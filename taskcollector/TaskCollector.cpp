#include "FreeImage.h"
#include <algorithm>
#include <iostream>
#include <memory>
#include <stdlib.h>
#include <stdio.h>
#include <string>
#include <string.h>
#include <vector>

struct Deleter
{
	void operator()(FIBITMAP* ptr) const
	{
		FreeImage_Unload(ptr);
	}
};

using bitmap_ptr = std::unique_ptr<FIBITMAP, Deleter>;

/** Generic image loader
@param lpszPathName Pointer to the full file name
@param flag Optional load flag constant
@return Returns the loaded dib if successful, returns NULL otherwise
*/
bitmap_ptr GenericLoader(const std::string& lpszPathName, int flag) {
	auto fif = FIF_UNKNOWN;

	// check if file path is not empty
	if (lpszPathName.empty())
		return nullptr;

	// check the file signature and deduce its format
	// (the second argument is currently not used by FreeImage)
	fif = FreeImage_GetFileType(lpszPathName.c_str(), 0);
	if (fif == FIF_UNKNOWN) {
		// no signature ?
		// try to guess the file format from the file extension
		fif = FreeImage_GetFIFFromFilename(lpszPathName.c_str());
	}
	// check that the plugin has reading capabilities ...
	if ((fif != FIF_UNKNOWN) && FreeImage_FIFSupportsReading(fif)) {
		// ok, let's load the file
		bitmap_ptr dib(FreeImage_Load(fif, lpszPathName.c_str(), flag));
		// unless a bad file format, we are done !
		return dib;
	}
	return nullptr;
}

/** Generic image writer
@param dib Pointer to the dib to be saved
@param lpszPathName Pointer to the full file name
@param flag Optional save flag constant
@return Returns true if successful, returns false otherwise
*/
bool GenericWriter(const bitmap_ptr& dib, const std::string& lpszPathName, int flag) {
	auto fif = FIF_UNKNOWN;
	auto bSuccess = FALSE;
	// check if file path is not empty
	if (lpszPathName.empty())
		return false;
	if (dib) {
		// try to guess the file format from the file extension
		fif = FreeImage_GetFIFFromFilename(lpszPathName.c_str());
		if (fif != FIF_UNKNOWN) {
			// check that the plugin has sufficient writing and export capabilities ...
			if (FreeImage_FIFSupportsWriting(fif) && FreeImage_FIFSupportsExportType(fif, FreeImage_GetImageType(dib.get()))) {
				// ok, we can save the file
				bSuccess = FreeImage_Save(fif, dib.get(), lpszPathName.c_str(), flag);
				// unless an abnormal bug, we are done !
			}
			else {
				std::cout << "Can't save file" << lpszPathName << std::endl;
			}
		}
	}
	return (bSuccess == TRUE);
}

// ----------------------------------------------------------

/**
FreeImage error handler
@param fif Format / Plugin responsible for the error
@param message Error message
*/
void FreeImageErrorHandler(FREE_IMAGE_FORMAT fif, const char *message) {
	printf("\n*** ");
	if (fif != FIF_UNKNOWN) {
		printf("%s Format\n", FreeImage_GetFormatFromFIF(fif));
	}
	printf(message);
	printf(" ***\n");
}

// ----------------------------------------------------------

class TaskCollector {

protected:
	std::vector<bitmap_ptr> chunks;
	std::vector<bitmap_ptr> alphaChunks;

public:
	TaskCollector() = default;
	TaskCollector(const TaskCollector&) = delete;
	TaskCollector(TaskCollector &&other)
	{
		chunks = std::move(other.chunks);
		alphaChunks = std::move(other.alphaChunks);
	}
	~TaskCollector() = default;

	bool addImgFile(const std::string& pathName, int flag = 0)  {
		if (pathName.empty())
			return false;
		auto img = GenericLoader(pathName.c_str(), flag);
		if (!img)
			return false;
		chunks.emplace_back(std::move(img));
		return true;
	};

	bool addAlphaFile(const std::string& pathName, int flag = 0) {
		if (pathName.empty())
			return false;
		auto img = GenericLoader(pathName.c_str(), flag);
		if (!img)
			return false;
		alphaChunks.emplace_back(std::move(img));
		return true;
	};

	virtual bitmap_ptr finalize(bool showProgress = false) = 0;

	bool finalizeAndSave(const std::string& outputPath) {
		if (outputPath.empty())
			return false;
		std::cout << "finalize & safe " << outputPath << std::endl;
		auto img = finalize();
		return GenericWriter(img, outputPath, EXR_FLOAT);
	};

};

class AddTaskCollector : public TaskCollector {

public:
	bitmap_ptr finalize(bool showProgress = false) {
		if (chunks.empty()) {
			return nullptr;
		}
		if (showProgress) {
			printf("Adding all accepted chunks to the final image\n");
		}

		const auto it = chunks.begin();
		const auto width = FreeImage_GetWidth(it->get());
		const auto height = FreeImage_GetHeight(it->get());
		const auto type = FreeImage_GetImageType(it->get());

		bitmap_ptr finalImage(FreeImage_Copy(it->get(), 0, height, width, 0));

		auto RGBChunkWorker = [=, &finalImage](const bitmap_ptr& el)
		{
			for (unsigned int y = 0; y < height; ++y) {
				auto srcbits = reinterpret_cast<FIRGBF *>(FreeImage_GetScanLine(el.get(), y));
				auto dstbits = reinterpret_cast<FIRGBF *>(FreeImage_GetScanLine(finalImage.get(), y));

				for (unsigned int x = 0; x < width; ++x) {
					dstbits[x].red += srcbits[x].red;
					dstbits[x].blue += srcbits[x].blue;
					dstbits[x].green += srcbits[x].green;
				}
			}
		};

		auto RGBAChunkWorker = [=, &finalImage](const bitmap_ptr& el)
		{
			for (unsigned int y = 0; y < height; ++y) {
				const auto srcbits = reinterpret_cast<FIRGBAF *>(FreeImage_GetScanLine(el.get(), y));
				auto dstbits = reinterpret_cast<FIRGBAF *>(FreeImage_GetScanLine(finalImage.get(), y));

				for (unsigned int x = 0; x < width; ++x) {
					dstbits[x].red += srcbits[x].red;
					dstbits[x].blue += srcbits[x].blue;
					dstbits[x].green += srcbits[x].green;
					dstbits[x].alpha += srcbits[x].alpha;
				}
			}
		};

		auto alphaChunksWorker = [height, width, &finalImage](bitmap_ptr& el)
		{
			for (unsigned int y = 0; y < height; ++y) {
				const auto srcbits = (FIRGBAF *)FreeImage_GetScanLine(el.get(), y);
				auto dstbits = (FIRGBAF *)FreeImage_GetScanLine(finalImage.get(), y);

				for (unsigned int x = 0; x < width; ++x) {
					dstbits[x].alpha += srcbits[x].red + srcbits[x].blue + srcbits[x].green;
				}
			}
		};

		if (type == FIT_RGBF)
			std::for_each(std::next(chunks.begin()), chunks.end(), RGBChunkWorker);
		else if (type == FIT_RGBAF)
			std::for_each(std::next(chunks.begin()), chunks.end(), RGBAChunkWorker);
		std::for_each(alphaChunks.begin(), alphaChunks.end(), alphaChunksWorker);

		return finalImage;
	}
};

class PasteTaskCollector : public TaskCollector {

public:
	bitmap_ptr finalize(bool showProgress = true) {
		if (chunks.empty()) {
			return nullptr;
		}
		if (showProgress) {
			printf("Adding all accepted chunks to the final image\n");
		}
		const auto it = chunks.begin();
		const auto width = FreeImage_GetWidth(it->get());
		const auto chunkHeight = FreeImage_GetHeight(it->get());
		const auto height = chunkHeight * chunks.size();
		auto currentHeight = height - chunkHeight;

		const auto type = FreeImage_GetImageType(it->get());
		const auto bpp = FreeImage_GetBPP(it->get());
		bitmap_ptr finalImage(FreeImage_AllocateT(type, width, height, bpp));

		auto RGBChunkWorker = [=, &finalImage, &currentHeight](const bitmap_ptr& el)
		{
			for (unsigned int y = 0; y < chunkHeight; ++y) {
				const auto srcbits = reinterpret_cast<FIRGBF *>(FreeImage_GetScanLine(el.get(), y));
				auto dstbits = reinterpret_cast<FIRGBF *>(FreeImage_GetScanLine(finalImage.get(), y + currentHeight));
				for (unsigned int x = 0; x < width; ++x) {
					dstbits[x].red = srcbits[x].red;
					dstbits[x].blue = srcbits[x].blue;
					dstbits[x].green = srcbits[x].green;
				}
			}
			currentHeight -= chunkHeight;
		};

		auto RGBAChunkWorker = [=, &finalImage, &currentHeight](const bitmap_ptr& el)
		{
			for (unsigned int y = 0; y < chunkHeight; ++y) {
				const auto srcbits = reinterpret_cast<FIRGBAF *>(FreeImage_GetScanLine(el.get(), y));
				auto dstbits = reinterpret_cast<FIRGBAF *>(FreeImage_GetScanLine(finalImage.get(), y + currentHeight));
				for (unsigned int x = 0; x < width; ++x) {
					dstbits[x].red = srcbits[x].red;
					dstbits[x].blue = srcbits[x].blue;
					dstbits[x].green = srcbits[x].green;
					dstbits[x].alpha = srcbits[x].alpha;
				}
			}
			currentHeight -= chunkHeight;
		};

		if (type == FIT_RGBF)
			std::for_each(chunks.begin(), chunks.end(), RGBChunkWorker);
		else if (type == FIT_RGBAF)
			std::for_each(chunks.begin(), chunks.end(), RGBAChunkWorker);

		return finalImage;
	}
};

int
main(int argc, char *argv[]) {
	// call this ONLY when linking with FreeImage as a static library
#ifdef FREEIMAGE_LIB
	FreeImage_Initialise();
#endif // FREEIMAGE_LIB

	// initialize your own FreeImage error handler
	FreeImage_SetOutputMessage(FreeImageErrorHandler);

	// print version & copyright infos
	printf("FreeImage version : %s", FreeImage_GetVersion());
	printf("\n");
	printf(FreeImage_GetCopyrightMessage());
	printf("\n");

	if (argc < 4)  {
		printf("Usage: taskcollector.exe <type> <outputfile> <inputfile1> [<input file2> ...]\n");
		return -1;
	}

	std::unique_ptr<TaskCollector> taskCollector;
	std::unique_ptr<TaskCollector> alphaTaskCollector;

	if (strcmp(argv[1], "add") == 0) {
		taskCollector = std::make_unique<AddTaskCollector>();
		alphaTaskCollector = std::make_unique<AddTaskCollector>();
	}
	else if (strcmp(argv[1], "paste") == 0) {
		taskCollector = std::make_unique<PasteTaskCollector>();
		alphaTaskCollector = std::make_unique<PasteTaskCollector>();
	}
	else {
		printf("Possible types: 'add', 'paste'\n");
		return -1;
	}

	for (int i = 3; i < argc; ++i) {
		if (std::string(argv[i]).find("Alpha") == std::string::npos) {
			if (!taskCollector->addImgFile(argv[i])) {
				printf("Can't add file: %s\n", argv[i]);
			}
		}
		else {
			if (!taskCollector->addAlphaFile(argv[i])) {
				printf("Can't add file: %s\n", argv[i]);
			}
			if (!alphaTaskCollector->addImgFile(argv[i])) {
				printf("Can't add file: %s\n", argv[i]);
			}
		}
	}

	std::string name(argv[2]);
	auto it = name.find_last_of('.');
	name = ( (it == std::string::npos) ? (name + ".exr") : (name.substr(0, it) + ".Alpha.exr") );

	taskCollector->finalizeAndSave(argv[2]);
	alphaTaskCollector->finalizeAndSave(name);
	// call this ONLY when linking with FreeImage as a static library
#ifdef FREEIMAGE_LIB
	FreeImage_DeInitialise();
#endif // FREEIMAGE_LIB

	return 0;
}