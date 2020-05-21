float3 HuetoRGB(float hue) {
	return saturate(float3(abs(hue * 6 - 3) - 1, 2 - abs(hue * 6 - 2), 2 - abs(hue * 6 - 4)));
}
float3 HSVtoRGB(float3 hsv) {
	float3 rgb = HuetoRGB(hsv.x);
	return ((rgb - 1) * hsv.y + 1) * hsv.z;
}
float3 RGBtoHCV(float3 rgb) {
	// Based on work by Sam Hocevar and Emil Persson
	float4 P = (rgb.g < rgb.b) ? float4(rgb.bg, -1.0, 2.0 / 3.0) : float4(rgb.gb, 0.0, -1.0 / 3.0);
	float4 Q = (rgb.r < P.x) ? float4(P.xyw, rgb.r) : float4(rgb.r, P.yzx);
	float C = Q.x - min(Q.w, Q.y);
	float H = abs((Q.w - Q.y) / (6 * C + 1e-6) + Q.z);
	return float3(H, C, Q.x);
}
float3 RGBtoHSV(float3 rgb) {
	float3 hcv = RGBtoHCV(rgb);
	return float3(hcv.x, hcv.y / (hcv.z + 1e-6), hcv.z);
}
float ChromaKey(float3 hsv, float3 key) {
	float3 d = abs(hsv - key) / float3(0.1, 0.5, 0.5);
	return saturate(length(d) - 1);
}

float ThresholdDensity(float x) {
	x = (x - RemapRange.x) / (RemapRange.y - RemapRange.x);
	return saturate(x);// * saturate(x);
}
float4 ComputeColor(float4 c) {
	#if defined(NON_BAKED_RGBA) || defined(NON_BAKED_R_COLORIZE) || defined(NON_BAKED_R)
	// non-baked volume, do processing

	#ifdef NON_BAKED_R_COLORIZE
	c.rgb = HSVtoRGB(float3(HueRange.x + c.a * (HueRange.y - HueRange.x), .5, 1));

	#elif defined(NON_BAKED_RGBA)
	// chroma-key blue out (for visible human dataset)
	float3 hsv = RGBtoHSV(c.rgb);
	c.a *= ChromaKey(hsv, RGBtoHSV(float3(0.07059, 0.07843, 0.10589))) * saturate(4 * hsv.z);
	#endif

	c.a *= ThresholdDensity(c.a);
	#endif
	return c;
}
float4 ApplyMask(float4 c, uint3 index) {
	#ifdef MASK_COLOR
	static const float3 maskColors[8] = {
		float3(1.0, 0.1, 0.1),
		float3(0.1, 1.0, 0.1),
		float3(0.1, 0.1, 1.0),

		float3(1.0, 1.0, 0.0),
		float3(1.0, 0.1, 1.0),
		float3(0.1, 1.0, 1.0),

		float3(1.0, 0.5, 0.1),
		float3(1.0, 0.1, 0.5),
	};

	uint value = RawMask[index] & MaskValue;
	if (value) c.rgb = maskColors[firstbitlow(value)];
	#endif
	return c;
}

float4 SampleColor(uint3 index){
	float4 c = Volume[index];
	c = ComputeColor(c);
	c = ApplyMask(c, index);
	return c;
}
float4 SampleColor(float3 uvw) {
	float4 c = Volume.SampleLevel(Sampler, uvw, 0);
	c = ComputeColor(c);
	c = ApplyMask(c, clamp(uint3(uvw * VolumeResolution + 0.5), 0, VolumeResolution - 1));
	return c;
}
float4 SampleColor(float3 uvw, int3 offset) {
	float4 c = Volume.SampleLevel(Sampler, uvw, 0, offset);
	c = ComputeColor(c);
	c = ApplyMask(c, clamp(uint3(uvw * VolumeResolution + 0.5) + offset, 0, VolumeResolution - 1));
	return c;
}

float3 SampleGradient(float3 uvw) {
	#ifdef GRADIENT_TEXTURE
	return Gradient[clamp(uint3(uvw * VolumeResolution + 0.5), 0, VolumeResolution - 1)].xyz;
	#else
	return float3(
		SampleColor(uvw, int3(1, 0, 0)).a - SampleColor(uvw, int3(-1, 0, 0)).a,
		SampleColor(uvw, int3(0, 1, 0)).a - SampleColor(uvw, int3(0, -1, 0)).a,
		SampleColor(uvw, int3(0, 0, 1)).a - SampleColor(uvw, int3(0, 0, -1)).a);
	#endif
}