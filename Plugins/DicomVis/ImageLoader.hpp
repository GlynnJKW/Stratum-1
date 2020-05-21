#pragma once

#include <Content/Texture.hpp>

enum ImageStackType {
	IMAGE_STACK_NONE,
	IMAGE_STACK_DICOM,
	IMAGE_STACK_RAW,
	IMAGE_STACK_STANDARD,
};

struct ScanInfo {
	ImageStackType type;
	SYSTEMTIME last_write;
	std::string path;
	std::string study_name;
	std::string patient_id;
	std::string patient_name;
	std::string study_date;
	std::string study_time;
	uint8_t num_slices;
	bool has_mask;

	bool operator< (const ScanInfo info) const { return study_date < info.study_date; }
};

class ImageLoader {
public:
	PLUGIN_EXPORT static ImageStackType FolderStackType(const fs::path& folder);
	// Load a stack of RAW images
	// Load a stack of normal images (png, jpg, tiff, etc..)
	// Items are sorted in order of name
	PLUGIN_EXPORT static Texture* LoadStandardStack(const fs::path& folder, Device* device, float3* size, bool reverse = false, uint32_t channelCount = 0, bool unorm = true);

	// Get metadata from a set of DICOM images
	PLUGIN_EXPORT static ScanInfo GetScanInfo(const fs::path& folder, ImageStackType type);

	// Load a stack of RAW images
	// PLUGIN_EXPORT static Texture* LoadRawStack(const std::string& folder, Device* device, float3* size);
	PLUGIN_EXPORT static Texture* LoadDicomStack(const fs::path& folder, Device* device, float3* size, float4x4* orientation = nullptr);
	// Load a stack of raw, uncompressed images
	// Items are sorted in order of name
	PLUGIN_EXPORT static Texture* LoadRawStack(const fs::path& folder, Device* device, float3* size);
};