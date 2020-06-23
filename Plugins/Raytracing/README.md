# GPU Raytracing

The aim of this plugin is to perform path tracing on the GPU in real-time. In order to acheive this, the integrator has been tuned to spread samples across time and re-accumulate samples between frames whenever possible.

Currently, the spatial accumulator is disabled as it over-blurs the image, but temporal accumulation works fine if the camera doesn't move (in other words, there is NOT any reprojection happening currently).

In addition to simply tracing rays on the GPU, this project also aims to simulate simple volumetrics and refraction effects.

## Features
- Volumetric absorption and scattering
- Importance environment map sampling
- Arbitrary mesh lights
- Textured surfaces (including bump mapping)
- Correlated multi-jittered sampling
- In-app GUI for tweaking scene settings on-the-fly
- Simple metallic/roughness workflow
- Probablistic BxDF evaluation (supports multiple BxDF "closures" per-material)
- Single-scatter GGX specular reflection and refraction, with Oren-Nayar diffuse model

# Volumetric Effects
Volume absorption is implemented simply by applying Beer's Law whenever a ray travels through a medium. Scattering is somewhat more complicated, as to accurately simulate the results of scattering, many more rays must be scattered out from the volume. My environment map importance sampling helps with this to some degree, but it still requires much time to converge to an acceptable result. Scattered rays are directed towards bright spots on the environment map in an effort to importance-sample the sample space.

Environment map importance sampling helps caustics converge quickly, and in combination with volumetric effects, produces interesting results:
![crystal](https://i.imgur.com/N2YJB0p.png)

Using Stratum's immediate-mode GUI to create a simple set of controls helps immensely with material tweaking:
![gui](https://i.imgur.com/iqXh319.png)

# Environment Map Importance Sampling
In order to sample the environment map in a less arbitrary way, I created an algorithm that traverses the environment map's mipmap tree to randomly select a bright spot. The algorithm works as follows:
- Start at the second highest mip map (2x2 or 1x2 resolution)
- Use the brightness of each pixel to randomly select one of the four
- Repeat on the next mipmap level (which is 2x the resolution) but on the quadrant selected from the previous step

This algorithm runs only for a few of the mipmap levels, as each iteration effectively halves the region of the texture that will be sampled. It then randomly selects a direction pointing in the direction of the last quadrant selected.

## Metallic/Roughness Workflow
![Oren-Nayar](https://i.imgur.com/Dfn6NNq.png)

In order to simplify the creation of materials, the exposed parameters collapse to "metallic" and "roughness" (primarily, other settings such as transmission exist still). This is to hide the "diffuse/specular" workflow that is often easily abused to break realism. In practice, most substances can be classified as either "metal" or "non-metal" (specifically, "conductor" or "insulator"). We can then observe that metals absorb light incredibly fast, meaning they only "reflect" specular light paths (off the microsurface of the substance), whereas insulators are capable of allowing light to bounce around inside their microsurfaces and reflect it in diffuse ways (or transmit it, in the case of glass).

Keeping these things in mind, we can specify a material's "Base Color", "Metallicity" and "Roughness" and then come up with the actual specular and diffuse contributions of a BxDF using this information.

![Bronze ball](https://i.imgur.com/oYmvGXl.png)

In this plugin, the diffuse reflectivity of a material is defined as
`(1 - Metallic) * (1 - Transmission)` where `Metallic` and `Transmission` are in the range [0, 1]. This diffuse weight is used to select either an Oren-Nayar diffuse BSDF, or a microfacet BSDF.

The microfacet BSDF samples from two GGX distributions (taking into account both reflective roughness and transmission roughness) and then selects transmission rays with probability `(1 - fresnel) * (1 - Metallic) * Transmission`. This accounts for the fact that not all non-metals exhibit transmission, but virtually all metals are incapable of transmission. Additionally, it also uses the full Fresnel equations to compute accurate Fresnel reflectance.

## BxDF Closure Workflow
In order to be as flexible as possible, the way BxDF functions are evaluated is probablistic and depends on the "weight" of each BxDF. This allows more complicated materials (such as the ones in these images) to be defined as combinations of simpler light transport functions. Currently, the supported BxDF "closures" are:
- Oren-Nayar Diffuse BRDF
- Microfacet BSDF (with transmission and volumetric properties)
- Emission

In the future, more specialized BxDFs such as hair, cloth or clearcoated materials might be desireable, to which this system is extremely easily to extend.


# Extra: Development Screenshot
Here is the first image I produced with volumetric effects working:
![yeet](https://i.imgur.com/Oet2t3v.png)