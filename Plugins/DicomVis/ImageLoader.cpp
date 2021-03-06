#include "ImageLoader.hpp"

#include <thread>
#include <atomic>

#define STB_IMAGE_IMPLEMENTATION
#include <ThirdParty/stb_image.h>

#include <dcmtk/dcmimgle/dcmimage.h>
#include <dcmtk/dcmdata/dctk.h>

using namespace std;

unordered_multimap<string, ImageStackType> ExtensionMap {
	{ ".dcm", IMAGE_STACK_DICOM },
	{ ".raw", IMAGE_STACK_RAW },
	{ ".png", IMAGE_STACK_STANDARD },
	{ ".jpg", IMAGE_STACK_STANDARD }
};

ImageStackType ImageLoader::FolderStackType(const fs::path& folder) {
	try {
		if (!fs::exists(folder)) return IMAGE_STACK_NONE;

		ImageStackType type = IMAGE_STACK_NONE;
		for (const auto& p : fs::directory_iterator(folder))
			if (ExtensionMap.count(p.path().extension().string())) {
				ImageStackType t = ExtensionMap.find(p.path().extension().string())->second;
				if (type != IMAGE_STACK_NONE && type != t) return IMAGE_STACK_NONE; // inconsistent image type
				type = t;
			}

		if (type == IMAGE_STACK_NONE) return type;

		if (type == IMAGE_STACK_STANDARD) {
			vector<fs::path> images;
			for (const auto& p : fs::directory_iterator(folder))
				if (ExtensionMap.count(p.path().extension().string()))
					images.push_back(p.path());
			std::sort(images.begin(), images.end(), [](const fs::path& a, const fs::path& b) {
				return a.stem().string() < b.stem().string();
			});

			int x, y, c;
			if (stbi_info(images[0].string().c_str(), &x, &y, &c) == 0) return IMAGE_STACK_NONE;

			uint32_t width = x;
			uint32_t height = y;
			uint32_t channels = c;
			uint32_t depth = images.size();

			for (uint32_t i = 1; i < images.size(); i++) {
				stbi_info(images[i].string().c_str(), &x, &y, &c);
				if (x != width) { return IMAGE_STACK_NONE; }
				if (y != height) { return IMAGE_STACK_NONE; }
				if (c != channels) { return IMAGE_STACK_NONE; }
			}
		}
		return type;
	} catch (exception e) {
		return IMAGE_STACK_NONE;
	}
}

Texture* ImageLoader::LoadStandardStack(const fs::path& folder, Device* device, float3* scale, bool reverse, uint32_t channelCount, bool unorm) {
	if (!fs::exists(folder)) return nullptr;

	vector<fs::path> images;
	for (const auto& p : fs::directory_iterator(folder))
		if (ExtensionMap.count(p.path().extension().string()) && ExtensionMap.find(p.path().extension().string())->second == IMAGE_STACK_STANDARD)
			images.push_back(p.path());
	if (images.empty()) return nullptr;
	std::sort(images.begin(), images.end(), [reverse](const fs::path& a, const fs::path& b) {
		string astr = a.stem().string();
		string bstr = b.stem().string();
		if (astr.find_first_not_of( "0123456789" ) == string::npos && bstr.find_first_not_of( "0123456789" ) == string::npos)
			return reverse ? atoi(astr.c_str()) > atoi(bstr.c_str()) : atoi(astr.c_str()) < atoi(bstr.c_str());
		return reverse ? astr > bstr : astr < bstr;
	});

	int x, y, c;
	stbi_info(images[0].string().c_str(), &x, &y, &c);
	
	uint32_t depth = images.size();
	uint32_t width = x;
	uint32_t height = y;
	uint32_t channels = channelCount == 0 ? c : channelCount;

	VkFormat format;
	switch (channels) {
	case 4:
		format = unorm ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_R8G8B8A8_UINT;
		break;
	case 3:
		format = unorm ? VK_FORMAT_R8G8B8_UNORM : VK_FORMAT_R8G8B8_UINT;
		break;
	case 2:
		format = unorm ? VK_FORMAT_R8G8_UNORM : VK_FORMAT_R8G8_UINT;
		break;
	case 1:
		format = unorm ? VK_FORMAT_R8_UNORM : VK_FORMAT_R8_UINT;
		break;
	default:
		return nullptr; // ??
	}

	size_t sliceSize = width * height * channels;
	uint8_t* pixels = new uint8_t[sliceSize * depth];
	memset(pixels, 0, sliceSize * depth);

	uint32_t done = 0;
	vector<thread> threads;
	uint32_t threadCount = thread::hardware_concurrency();
	for (uint32_t j = 0; j < threadCount; j++) {
		threads.push_back(thread([=, &done]() {
			int xt, yt, ct;
			for (uint32_t i = j; i < images.size(); i += threadCount) {
				stbi_uc* img = stbi_load(images[i].string().c_str(), &xt, &yt, &ct, 0);
				if (xt == width || yt == height) {
					if (ct == channels)
						memcpy(pixels + sliceSize * i, img, sliceSize);
					else {
						uint8_t* slice = pixels + sliceSize * i;
						for (uint32_t y = 0; y < height; y++)
							for (uint32_t x = 0; x < width; x++)
								for (uint32_t k = 0; k < min((uint32_t)ct, channels); k++)
									slice[channels * (y * width + x) + k] = img[ct * (y * width + x) + k];
					}
				}
				stbi_image_free(img);

				done++;
			}
		}));
	}
	printf("Loading stack");
	while (done < images.size()) {
		printf("\rLoading stack: %u/%u    ", done, (uint32_t)images.size());
		this_thread::sleep_for(10ms);
	}
	for (thread& t : threads) t.join();
	printf("\rLoading stack: Done           \n");

	Texture* volume = new Texture(folder.string(), device, pixels, sliceSize*depth, width, height, depth, format, 1, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
	delete[] pixels;

	if (scale) *scale = float3(.05f, .05f, .05f);

	return volume;
}

struct DcmSlice {
	DicomImage* image;
	double3 spacing;
	double3 orientationU;
	double3 orientationV;
	double location;
};
DcmSlice ReadDicomSlice(const string& file) {
	DcmFileFormat fileFormat;
	fileFormat.loadFile(file.c_str());
	DcmDataset* dataset = fileFormat.getDataset();

	double3 s = 0;
	dataset->findAndGetFloat64(DCM_PixelSpacing, s.x, 0);
	dataset->findAndGetFloat64(DCM_PixelSpacing, s.y, 1);
	dataset->findAndGetFloat64(DCM_SliceThickness, s.z, 0);

	double3 u = 0;
	dataset->findAndGetFloat64(DCM_ImageOrientationPatient, u.x, 0);
	dataset->findAndGetFloat64(DCM_ImageOrientationPatient, u.y, 1);
	dataset->findAndGetFloat64(DCM_ImageOrientationPatient, u.z, 2);

	double3 v = 0;
	dataset->findAndGetFloat64(DCM_ImageOrientationPatient, v.x, 3);
	dataset->findAndGetFloat64(DCM_ImageOrientationPatient, v.y, 4);
	dataset->findAndGetFloat64(DCM_ImageOrientationPatient, v.z, 5);

	double x = 0;
	dataset->findAndGetFloat64(DCM_SliceLocation, x, 0);

	return { new DicomImage(file.c_str()), s, u, v, x };
}
Texture* ImageLoader::LoadDicomStack(const fs::path& folder, Device* device, float3* size, float4x4* orientation) {
	if (!fs::exists(folder)) return nullptr;

	double3 maxSpacing = 0;
	vector<DcmSlice> images = {};
	for (const auto& p : fs::directory_iterator(folder))
		if (ExtensionMap.count(p.path().extension().string()) && ExtensionMap.find(p.path().extension().string())->second == IMAGE_STACK_DICOM) {
			images.push_back(ReadDicomSlice(p.path().string()));
			maxSpacing = max(maxSpacing, images[images.size() - 1].spacing);
		}

	if (images.empty()) return nullptr;

	std::sort(images.begin(), images.end(), [](const DcmSlice& a, const DcmSlice& b) {
		return a.location < b.location;
	});

	uint32_t w = images[0].image->getWidth();
	uint32_t h = images[0].image->getHeight();
	uint32_t d = (uint32_t)images.size();

	if (w == 0 || h == 0) return nullptr;

	// volume size in meters
	if (size) {
		float2 b = images[0].location;
		for (auto i : images) {
			b.x = (float)fmin(i.location - i.spacing.z * .5, b.x);
			b.y = (float)fmax(i.location + i.spacing.z * .5, b.y);
		}

		*size = float3(.001 * double3(maxSpacing.xy * double2(w, h), b.y - b.x));
		printf("%fm x %fm x %fm\n", size->x, size->y, size->z);
	}

	if (orientation) {
		double3 row = images[0].orientationU;
		double3 col = images[0].orientationV;
		double3 dir = cross(col, row);
		if (dir.x == 0 && dir.y == 0 && dir.z == 0) {
			*orientation = float4x4(1);
		}
		else {
			*orientation = float4x4(
				row.x, row.y, row.z, 0,
				col.x, col.y, col.z, 0,
				dir.x, dir.y, dir.z, 0,
				0, 0, 0, 1);
		}
	}

	uint16_t* data = new uint16_t[w * h * d];
	memset(data, 0, w * h * d * sizeof(uint16_t));
	for (uint32_t i = 0; i < images.size(); i++) {
		images[i].image->setMinMaxWindow();
		uint16_t* pixels = (uint16_t*)images[i].image->getOutputData(16);
		memcpy(data + i * w * h, pixels, w * h * sizeof(uint16_t));
	}

	Texture* tex = new Texture(folder.string(), device, data, w*h*d*sizeof(uint16_t), w, h, d, VK_FORMAT_R16_UNORM, 1, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT);
	delete[] data;
	for (auto& i : images) delete i.image;
	return tex;
}

ScanInfo ImageLoader::GetScanInfo(const fs::path& folder, ImageStackType type) {
	ScanInfo info = { type, {}, folder.string(), folder.stem().string(), "Unknown", "Unknown", "Unknown", "Unknown", 0, false };

	auto filetime = fs::last_write_time(folder);
	FILETIME ft;
	memcpy(&ft, &filetime, sizeof(FILETIME));
	SYSTEMTIME  stSystemTime;
	FileTimeToSystemTime(&ft, &stSystemTime);
	info.last_write = stSystemTime;

	if (type == IMAGE_STACK_DICOM) {
		fs::path first = "";
		for (const auto& p : fs::directory_iterator(folder)) {
			if (ExtensionMap.count(p.path().extension().string()) && ExtensionMap.find(p.path().extension().string())->second == IMAGE_STACK_DICOM) {
				if (first.empty()) {
					first = p;
				}
				info.num_slices++;
			}
		}

		DcmFileFormat fileFormat;
		fileFormat.loadFile(first.string().c_str());
		DcmDataset* dataset = fileFormat.getDataset();
		const char *patid, *patname, *studate, *stutime;
		dataset->findAndGetString(DCM_PatientID, patid);
		if (patid) info.patient_id = patid;
		dataset->findAndGetString(DCM_PatientName, patname);
		if (patname) info.patient_name = patname;
		dataset->findAndGetString(DCM_StudyDate, studate);
		if (studate) info.study_date = studate;
		dataset->findAndGetString(DCM_StudyTime, stutime);
		if (stutime) info.study_time = stutime;
	}

	if (fs::exists(folder.string() + "/mask") && fs::is_directory(folder.string() + "/mask")) {
		info.has_mask = true;
	}

	return info;
}

Texture* ImageLoader::LoadRawStack(const fs::path& folder, Device* device, float3* scale) {
	if (!fs::exists(folder)) return nullptr;

	vector<fs::path> images;
	for (const auto& p : fs::directory_iterator(folder))
		if (ExtensionMap.count(p.path().extension().string()) && ExtensionMap.find(p.path().extension().string())->second == IMAGE_STACK_RAW)
			images.push_back(p.path());
	if (images.empty()) return nullptr;
	std::sort(images.begin(), images.end(), [](const fs::path& a, const fs::path& b) {
		return a.string() < b.string();
	});

	uint32_t width = 2048;
	uint32_t height = 1216;
	uint32_t depth = images.size();

	size_t pixelCount = width * height;

	size_t sliceSize = pixelCount * 4;
	uint8_t* pixels = new uint8_t[sliceSize * depth];
	memset(pixels, 0, sliceSize * depth);

	uint32_t done = 0;
	vector<thread> threads;
	uint32_t threadCount = thread::hardware_concurrency();
	for (uint32_t j = 0; j < threadCount; j++) {
		threads.push_back(thread([=, &done]() {
			for (uint32_t i = j; i < images.size(); i += threadCount) {
				vector<uint8_t> slice;
				if (!ReadFile(images[i].string(), slice)) {
					fprintf_color(COLOR_RED, stderr, "Failed to read file %s\n", images[i].string().c_str());
					throw;
				}

				uint8_t* sliceStart = pixels + sliceSize * i;
				for (uint32_t y = 0; y < height; y++)
					for (uint32_t x = 0; x < width; x++) {
						sliceStart[4 * (x + y * width) + 0] = slice[x + y * width];
						sliceStart[4 * (x + y * width) + 1] = slice[x + y * width + pixelCount];
						sliceStart[4 * (x + y * width) + 2] = slice[x + y * width + 2*pixelCount];
						sliceStart[4 * (x + y * width) + 3] = 0xFF;
					}

				done++;
			}
		}));
	}
	printf("Loading stack");
	while (done < images.size()) {
		printf("\rLoading stack: %u/%u    ", done, (uint32_t)images.size());
		this_thread::sleep_for(10ms);
	}
	for (thread& t : threads) t.join();
	printf("\rLoading stack: Done           \n");

	Texture* volume = new Texture(folder.string(), device, pixels, sliceSize * depth, width, height, depth, VK_FORMAT_R8G8B8A8_UNORM, 1, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
	delete[] pixels;

	if (scale) *scale = float3(.00033f * width, .00033f * height, .001f * depth);

	return volume;
}
