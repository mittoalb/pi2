#pragma once
#include <list>

#include "filesystem.h"
#include "image.h"
#include "utilities.h"
#include "io/raw.h"
#include "io/itllz4.h"
#include "math/aabox.h"
#include "io/zarrcodecs.h"
#include "generation.h"
#include "math/vec3.h"

namespace itl2
{
	using std::cout, std::endl;
	namespace io
	{
		// Forward declaration of io::getInfo to avoid header loop.
		bool getInfo(const std::string& filename, Vec3c& dimensions, ImageDataType& dataType, std::string& reason);
	}
	namespace zarr
	{
		namespace internals
		{

			inline string zarrMetadataFilename(const string& path)
			{
				return path + "/zarr.json";
			}

			inline string concurrentTagFile(const string& path)
			{
				return path + "/concurrent";
			}

			/**
			Writes Zarr metadata file.
			*/
			void writeMetadata(const std::string& path, const Vec3c& dimensions, ImageDataType dataType, const Vec3c& chunkSize, int fillValue, const std::list<ZarrCodec>& codecs);

			inline std::string chunkFile(const std::string& path, size_t dimensionality, const Vec3c& chunkIndex)
			{
				//TODO: use separator from zarr metadata
				string separator = string("/");
				string filename = path + string("/c");
				for (size_t n = 0; n < dimensionality; n++)
					filename += separator + toString(chunkIndex[n]);
				return filename;
			}

			inline std::string writesFolder(const std::string& chunkFile)
			{
				// the file is a flag
				// if it exists, the chunk is unsafe to write to because it is currently being written to.
				return chunkFile + "_writes";
			}

			inline Vec3c clampedChunkSize(const Vec3c& chunkIndex, const Vec3c& chunkSize, const Vec3c& datasetSize)
			{
				Vec3c datasetChunkStart = chunkIndex.componentwiseMultiply(chunkSize);
				Vec3c datasetChunkEnd = datasetChunkStart + chunkSize;
				for (size_t n = 0; n < datasetChunkEnd.size(); n++)
				{
					if (datasetChunkEnd[n] > datasetSize[n])
						datasetChunkEnd[n] = datasetSize[n];
				}
				Vec3c realChunkSize = datasetChunkEnd - datasetChunkStart;

				return realChunkSize;
			}

			/**
			Writes single NN5 chunk file.
			@param chunkIndex Index of the chunk to write. This is used to determine the correct output folder.
			@param chunkSize Chunk size of the dataset.
			@param startInChunkCoords Start position of the block to be written in the coordinates of the chunk.
			@param chunkStart Start position of the block to be written in the coordinates of image targetImg.
			@param writeSize Size of block to be written.
			*/
			template<typename pixel_t>
			void writeSingleChunk(const Image<pixel_t>& img, const std::string& path, const Vec3c& chunkIndex, const Vec3c& chunkSize, const Vec3c& datasetSize,
				const Vec3c& startInChunkCoords, const Vec3c& startInImageCoords, const Vec3c& writeSize, int fillValue, const std::list<ZarrCodec>& codecs)
			{
				// Build path to chunk folder.
				string filename = chunkFile(path, getDimensionality(datasetSize), chunkIndex);

				// Clamp write size to the size of the image.
				Vec3c imageChunkEnd = startInImageCoords + writeSize;
				for (size_t n = 0; n < imageChunkEnd.size(); n++)
				{
					if (imageChunkEnd[n] > img.dimension(n))
						imageChunkEnd[n] = img.dimension(n);
				}
				Vec3c realWriteSize = imageChunkEnd - startInImageCoords;

				Vec3c realChunkSize = clampedChunkSize(chunkIndex, chunkSize, datasetSize);

				realWriteSize = min(realWriteSize, realChunkSize);

				// Check if we are in an unsafe chunk where writing to the chunk file is prohibited.
				// Chunk is unsafe if its folder contains writes folder.
				string writesFolder = internals::writesFolder(filename);
				bool unsafe = fs::exists(writesFolder);

				std::cout << "writeSingleChunk unsafe=" << unsafe << std::endl;
				for (auto codec : codecs)
				{
					std::cout << "writeSingleChunk codec=" << toString(codec.name) << std::endl;

				}
				if (codecs.size() == 1)
				{
					//only bytes codec
					if (unsafe)
					{
						// Unsafe chunk: write to separate writes folder.
						filename = writesFolder + toString(startInChunkCoords.x) + string("-") +
							toString(startInChunkCoords.y) + string("-") + toString(startInChunkCoords.z);
						//print all writeblock parameters in one line
						writeBytesCodecBlock(img, filename, Vec3c(0, 0, 0), realWriteSize, startInImageCoords, realWriteSize,
							false);
					}
					else
					{
						// Safe chunk: write directly to the chunk file.
						writeBytesCodecBlock(img, filename, startInChunkCoords, realChunkSize, startInImageCoords,
							realWriteSize, false);
					}

				}
				else
				{
					throw ITLException("multiple codecs not yet supported (writeSingleChunk)");
				}
			}

			inline size_t countChunks(const Vec3c& imageDimensions, const Vec3c& chunkSize)
			{
				Vec3c chunkStart(0, 0, 0);
				size_t chunkCount = 0;
				while (chunkStart.z < imageDimensions.z)
				{
					while (chunkStart.y < imageDimensions.y)
					{
						while (chunkStart.x < imageDimensions.x)
						{
							chunkCount++;
							chunkStart.x += chunkSize.x;
						}
						chunkStart.x = 0;
						chunkStart.y += chunkSize.y;
					}
					chunkStart.x = 0;
					chunkStart.y = 0;
					chunkStart.z += chunkSize.z;
				}
				return chunkCount;
			}
			/**
			Call lambda(chunkIndex, chunkStart) for all chunks in an image of given dimensions and chunk size.
			*/
			template<typename F>
			void forAllChunks(const Vec3c& imageDimensions, const Vec3c& chunkSize, bool showProgressInfo, F&& lambda)
			{

				size_t maxSteps = 0;
				if (showProgressInfo)
					maxSteps = countChunks(imageDimensions, chunkSize);
				ProgressIndicator progress(maxSteps, showProgressInfo);

				Vec3c chunkStart(0, 0, 0);
				Vec3c chunkIndex(0, 0, 0);
				while (chunkStart.z < imageDimensions.z)
				{
					while (chunkStart.y < imageDimensions.y)
					{
						while (chunkStart.x < imageDimensions.x)
						{
							std::cout << "forAllChunks chunkIndex=" << chunkIndex << " chunkStart=" << chunkStart << std::endl;
							lambda(chunkIndex, chunkStart);
							progress.step();

							chunkIndex.x++;
							chunkStart.x += chunkSize.x;
						}

						chunkIndex.x = 0;
						chunkIndex.y++;
						chunkStart.x = 0;
						chunkStart.y += chunkSize.y;
					}
					chunkIndex.x = 0;
					chunkIndex.y = 0;
					chunkIndex.z++;
					chunkStart.x = 0;
					chunkStart.y = 0;
					chunkStart.z += chunkSize.z;
				}
			}

			/**
			Writes zarr chunk files.
			*/
			template<typename pixel_t>
			void writeChunks(const Image<pixel_t>& img,
				const std::string& path,
				const Vec3c& chunkSize,
				int fillValue,
				const std::list<ZarrCodec>& codecs,
				const Vec3c& datasetSize,
				bool showProgressInfo)
			{
				forAllChunks(datasetSize, chunkSize, showProgressInfo, [&](const Vec3c& chunkIndex, const Vec3c& chunkStart)
				{
				  writeSingleChunk(img, path, chunkIndex, chunkSize, datasetSize, Vec3c(0, 0, 0), chunkStart, chunkSize, fillValue, codecs);
				});
			}

			template<typename pixel_t>
			void writeChunksInRange(Image<pixel_t>& img, const std::string& path, const Vec3c& chunkSize, int fillValue, std::list<ZarrCodec>& codecs,
				const Vec3c& filePosition, const Vec3c& fileDimensions,
				const Vec3c& imagePosition,
				const Vec3c& blockDimensions,
				bool showProgressInfo)
			{
				// Writes block of image defined by (imagePosition, blockDimensions) to file (defined by path),
				// to location defined by filePosition.

				AABoxc imageBlock = AABoxc::fromPosSize(imagePosition, blockDimensions);

				AABoxc fileTargetBlock = AABoxc::fromPosSize(filePosition, blockDimensions);

				forAllChunks(fileDimensions, chunkSize, showProgressInfo, [&](const Vec3c& chunkIndex, const Vec3c& chunkStart)
				{
				  // This is done for all chunks in the output file.
				  // We will need to update the chunk if the file block to be written (fileTargetBlock)
				  // overlaps the current chunk.
				  AABoxc currentChunk = AABoxc::fromPosSize(chunkStart, chunkSize);
				  if (fileTargetBlock.overlapsExclusive(currentChunk))
				  {
					  // The region of the current chunk that will be updated is the intersection of the current chunk and the
					  // file block to be written.
					  // Here, the chunkUpdateRegion is in file coordinates.
					  AABoxc chunkUpdateRegion = fileTargetBlock.intersection(currentChunk);

					  // Convert chunkUpdateRegion to image coordinates
					  Vec3c imageUpdateStartPosition = chunkUpdateRegion.position() - filePosition + imagePosition;
					  AABoxc imageUpdateRegion = AABoxc::fromPosSize(imageUpdateStartPosition, chunkUpdateRegion.size());

					  // Convert chunkUpdateRegion to chunk coordinates by subtracting chunk start position.
					  chunkUpdateRegion = chunkUpdateRegion.translate(-chunkStart);

					  // Bug check:
					  if (chunkUpdateRegion.minc.x < 0 ||
						  chunkUpdateRegion.minc.y < 0 ||
						  chunkUpdateRegion.minc.z < 0 ||
						  chunkUpdateRegion.maxc.x > chunkSize.x ||
						  chunkUpdateRegion.maxc.y > chunkSize.y ||
						  chunkUpdateRegion.maxc.z > chunkSize.z)
						  throw ITLException(
							  string("Sanity check failed: Chunk update region ") + toString(chunkUpdateRegion) + string(" is inconsistent with chunk size ") + toString(chunkSize) + string("."));

					  if (imageUpdateRegion.minc.x < imagePosition.x ||
						  imageUpdateRegion.minc.y < imagePosition.y ||
						  imageUpdateRegion.minc.z < imagePosition.z ||
						  imageUpdateRegion.maxc.x > imagePosition.x + blockDimensions.x ||
						  imageUpdateRegion.maxc.y > imagePosition.y + blockDimensions.y ||
						  imageUpdateRegion.maxc.z > imagePosition.z + blockDimensions.z
						  )
						  throw ITLException(
							  string("Sanity check failed: Image update region ") + toString(imageUpdateRegion) + string(" is inconsistent with image position ") + toString(imagePosition)
								  + string(" and update block dimensions") + toString(blockDimensions) + string("."));

					  // Write chunk data only if the update region is non-empty.
					  if (chunkUpdateRegion.size().min() > 0)
						  writeSingleChunk(img, path, chunkIndex, chunkSize, fileDimensions, chunkUpdateRegion.position(), imageUpdateRegion.position(), chunkUpdateRegion.size(), fillValue, codecs);
				  }
				});
			}

			/**
			Reads zarr chunk files.
			*/
			//mark 4
			template<typename pixel_t>
			void readChunksInRange(Image<pixel_t>& img, const std::string& path,
				const Vec3c& chunkSize, int fillValue, std::list<ZarrCodec>& codecs,
				const Vec3c& datasetShape,
				const Vec3c& start, const Vec3c& end,
				bool showProgressInfo)
			{
				Image<pixel_t> imgCopy(img.dimensions(), fillValue);
				ImageDataWrapper<pixel_t> imgCopyWrapper(imgCopy, chunkSize);
				const AABoxc imageBox = AABoxc::fromMinMax(start, end);
				forAllChunks(datasetShape, chunkSize, showProgressInfo, [&](const Vec3c& chunkIndex, const Vec3c& chunkStart)
				{
				  AABox<coord_t> currentChunk = AABox<coord_t>::fromPosSize(chunkStart, chunkSize);
				  if (currentChunk.overlapsExclusive(imageBox))
				  {
					  std::list<ZarrCodec>::iterator codec = codecs.begin();
					  for (; codec != codecs.end() && codec->type == ZarrCodecType::ArrayArrayCodec; ++codec)
					  {
						  if (codec->name == ZarrCodecName::Transpose)
						  {
							  auto orderJSON = codec->configuration["order"];
							  //TODO check if valid order
							  Vec3c order = Vec3c(1, 1, 1);
							  order[0] = orderJSON[0].get<size_t>();
							  if (orderJSON.size() >= 2)
								  order[1] = orderJSON[1].get<size_t>();
							  if (orderJSON.size() >= 3)
								  order[2] = orderJSON[2].get<size_t>();
							  cout << "readChunkFile Transpose order=" << order << endl;
							  imgCopyWrapper.transpose(order);
						  }
						  else throw ITLException("codec not implemented: " + toString(codec->name));
					  }
					  assert(codec != codecs.end() && codec->type == ZarrCodecType::ArrayBytesCodec);
					  if (codec->name == ZarrCodecName::Bytes)
					  {
						  const Vec3c chunkStartInTarget = chunkStart - start;
						  const Vec3c readSize = currentChunk.intersection(imageBox).size();
						  string filename = chunkFile(path, getDimensionality(datasetShape), chunkIndex);
						  if (fs::is_directory(filename))
						  {
							  throw ITLException(filename + string(" is a directory, but it should be a file."));
						  }
						  if (fs::is_regular_file(filename))
						  {
							  readBytesCodec(imgCopyWrapper, filename, chunkStart);
							  copyValues(img, imgCopyWrapper.img, chunkStartInTarget);
						  }
						  else
						  {
							  cout << "no file: " << filename << endl;
							  // No file => all pixels in the block are fillValue.
							  draw<pixel_t>(img, AABoxc::fromPosSize(chunkStartInTarget, readSize), (pixel_t)fillValue);
						  }
					  }
					  else throw ITLException("codec not implemented: " + toString(codec->name));
				  }
				});
			}

			/**
			Checks that provided zarr information is correct and writes metadata.
			@param deleteOldData Contents of the dataset are usually deleted when a write process is started. Set this to false to delete only if the path contains an incompatible dataset, but keep the dataset if it seems to be the same than the current image.
			*/
			void handleExisting(const Vec3c& imageDimensions,
				ImageDataType imageDataType,
				const std::string& path,
				const Vec3c& chunkSize,
				int fillValue,
				const std::list<ZarrCodec>& codecs,
				bool deleteOldData);

			template<typename pixel_t>
			void write(const Image<pixel_t>& img, const std::string& path, const Vec3c& chunkSize, int fillValue, const std::list<ZarrCodec>& codecs, bool deleteOldData, bool showProgressInfo)
			{
				Vec3c dimensions = img.dimensions();
				internals::handleExisting(dimensions, img.dataType(), path, chunkSize, fillValue, codecs, deleteOldData);
				fs::create_directories(path);
				internals::writeMetadata(path, dimensions, img.dataType(), chunkSize, fillValue, codecs);
				internals::writeChunks(img, path, chunkSize, fillValue, codecs, dimensions, showProgressInfo);
			}
		}

		bool getInfo(const std::string& path, Vec3c& shape, bool& isNativeByteOrder, ImageDataType& dataType, Vec3c& chunkSize, std::list<ZarrCodec>& codecs, int& fillValue, std::string& reason);

		inline bool getInfo(const std::string& path, Vec3c& shape, ImageDataType& dataType, std::string& reason)
		{
			bool dummyIsNative;
			Vec3c dummyChunkSize;
			int fillValue;
			std::list<ZarrCodec> codecs;
			return getInfo(path, shape, dummyIsNative, dataType, dummyChunkSize, codecs, fillValue, reason);
		}

		inline const Vec3c DEFAULT_CHUNK_SIZE = Vec3c(1536, 1536, 1536);
		inline const std::list<ZarrCodec> DEFAULT_CODECS = { ZarrCodec(ZarrCodecName::Bytes) };
		/**
		Write an image to a zarr dataset.
		@param targetImg Image to write.
		@param path Name of the top directory of the nn5 dataset.
		*/
		template<typename pixel_t>
		void write(const Image<pixel_t>& img,
			const std::string& path,
			const Vec3c& chunkSize = DEFAULT_CHUNK_SIZE,
			int fillValue = 0,
			const std::list<ZarrCodec>& codecs = DEFAULT_CODECS,
			bool showProgressInfo = false)
		{
			Vec3c clampedChunkSize = chunkSize;
			clamp(clampedChunkSize, Vec3c(1, 1, 1), img.dimensions());
			internals::write(img, path, clampedChunkSize, fillValue, codecs, false, showProgressInfo);
		}

		/**
		Writes a block of an image to the specified location in an .zarr dataset.
		The output dataset is not truncated if it exists.
		If the output file does not exist, it is created.
		@param targetImg Image to write.
		@param filename Name of file to write.
		@param filePosition Position in the file to write to.
		@param fileDimension Total dimensions of the entire output file.
		@param imagePosition Position in the image where the block to be written starts.
		@param blockDimensions Dimensions of the block of the source image to write.
		*/
		template<typename pixel_t>
		void writeBlock(Image<pixel_t>& img, const std::string& path, const Vec3c& chunkSize, int fillValue, std::list<ZarrCodec>& codecs,
			const Vec3c& filePosition, const Vec3c& fileDimensions,
			const Vec3c& imagePosition,
			const Vec3c& blockDimensions,
			bool showProgressInfo = false)
		{
			fs::create_directories(path);
			internals::writeMetadata(path, fileDimensions, img.dataType(), chunkSize, fillValue, codecs);
			internals::writeChunksInRange(img, path, chunkSize, fillValue, codecs, filePosition, fileDimensions, imagePosition, blockDimensions, showProgressInfo);
		}



		/**
		Reads a part of a .zarr dataset to the given image.
		NOTE: Does not support out of bounds start position.
		@param targetImg Image where the data is placed. The size of the image defines the size of the block that is read.
		@param filename The name of the dataset to read.
		@param fileStart Start location of the read in the file. The size of the image defines the size of the block that is read.
		*/
		//mark 5,6
		template<typename pixel_t>
		void readBlock(Image<pixel_t>& img, const std::string& path, const Vec3c& fileStart, bool showProgressInfo = false)
		{
			bool isNativeByteOrder;
			Vec3c datasetShape;
			ImageDataType dataType;
			Vec3c chunkSize;
			int fillValue;
			std::list<ZarrCodec> codecs;
			string reason;
			std::cout << "readBlock" << std::endl;
			if (!itl2::zarr::getInfo(path, datasetShape, isNativeByteOrder, dataType, chunkSize, codecs, fillValue, reason))
				throw ITLException(string("Unable to read zarr dataset: ") + reason);

			if (dataType != img.dataType())
				throw ITLException(string("Expected data type is ") + toString(img.dataType()) + " but the zarr dataset contains data of type " + toString(dataType) + ".");

			if (fileStart.x < 0 || fileStart.y < 0 || fileStart.z < 0 || fileStart.x >= datasetShape.x || fileStart.y >= datasetShape.y || fileStart.z >= datasetShape.z)
				throw ITLException("Out of bounds start position in readBlock.");

			Vec3c cStart = fileStart;
			clamp(cStart, Vec3c(0, 0, 0), datasetShape);
			Vec3c cEnd = fileStart + img.dimensions();
			clamp(cEnd, Vec3c(0, 0, 0), datasetShape);

			internals::readChunksInRange(img, path, chunkSize, fillValue, codecs, datasetShape, cStart, cEnd, showProgressInfo);

			if (!isNativeByteOrder)
				swapByteOrder(img);
		}
		/**
		Reads a zarr dataset file to the given image.
		@param targetImg Image where the data is placed. The size of the image will be set based on the dataset contents.
		@param path Path to the root of the nn5 dataset.
		*/
		//mark 5
		template<typename pixel_t>
		void read(Image<pixel_t>& img, const std::string& path, bool showProgressInfo = false)
		{
			bool isNativeByteOrder;
			Vec3c dimensions;
			ImageDataType dataType;
			Vec3c chunkSize;
			int fillValue;
			std::list<ZarrCodec> codecs;
			string reason;
			if (!itl2::zarr::getInfo(path, dimensions, isNativeByteOrder, dataType, chunkSize, codecs, fillValue, reason))
				throw ITLException(string("Unable to read zarr dataset: ") + reason);
			img.ensureSize(dimensions);

			readBlock(img, path, Vec3c(0, 0, 0), showProgressInfo);
		}
	}
}