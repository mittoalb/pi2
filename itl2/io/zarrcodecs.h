#pragma once

#include <string>
#include <sstream> // Ensure you include this for stringstream

#include "json.h"
#include "utilities.h"
#include "zarrimagedatawrapper.h"

namespace itl2
{
	using std::cout, std::endl;
	namespace zarr
	{

		enum class ZarrCodecType
		{
			ArrayArrayCodec,
			ArrayBytesCodec,
			BytesBytesCodec,
		};

		enum class ZarrCodecName
		{
			Bytes,
			Transpose,
			Blosc,
		};
		//TODO save codec in struct instead of json
		namespace blosc
		{
			enum class shuffle
			{
				noshuffle = 0,
				shuffle = 1,
				bitshuffle = 2
			};
		}
	}

	template<>
	inline std::string toString(const zarr::ZarrCodecName& x)
	{
		switch (x)
		{
		case zarr::ZarrCodecName::Bytes:
			return "bytes";
		case zarr::ZarrCodecName::Transpose:
			return "transpose";
		case zarr::ZarrCodecName::Blosc:
			return "blosc";
		}
		throw ITLException("Invalid ZarrCodecName.");
	}

	template<>
	inline zarr::ZarrCodecName fromString(const std::string& str0)
	{
		std::string str = str0;
		toLower(str);
		if (str == "bytes")
			return zarr::ZarrCodecName::Bytes;
		if (str == "transpose")
			return zarr::ZarrCodecName::Transpose;
		if (str == "blosc")
			return zarr::ZarrCodecName::Blosc;

		throw ITLException(std::string("Invalid zarr codec name: ") + str);
	}

	namespace zarr
	{
		class ZarrCodec
		{
		 public:

			ZarrCodecType type;
			ZarrCodecName name;
			nlohmann::json configuration;

			ZarrCodec(const ZarrCodecName name, nlohmann::json config = nlohmann::json())
			{
				this->name = name;
				switch (name)
				{
				case ZarrCodecName::Bytes:
					this->type = ZarrCodecType::ArrayBytesCodec;
					parseBytesCodecConfig(config);
					break;
				case ZarrCodecName::Transpose:
					this->type = ZarrCodecType::ArrayArrayCodec;
					parseTransposeCodecConfig(config);
					break;
				case ZarrCodecName::Blosc:
					this->type = ZarrCodecType::BytesBytesCodec;
					parseBloscCodecConfig(config);
					break;
				default:
					throw ITLException(std::string("Invalid zarr codec"));
				}
				cout << "created codec: " << toString(this->name) << " " << (int)this->type << endl;
			}

			void parseBloscCodecConfig(nlohmann::json config = nlohmann::json())
			{
				this->configuration = config;
			}

			void parseTransposeCodecConfig(nlohmann::json config = nlohmann::json())
			{
				this->configuration = config;
			}

			nlohmann::json toJSON() const
			{
				nlohmann::json j;
				j["name"] = toString(this->name);
				j["configuration"] = configuration; //this does not return by reference to the configuration just a copy, right?
				return j;
			}

			bool operator==(const ZarrCodec& t) const
			{
				return toJSON() == t.toJSON();
			}

			// BytesCodec
			void parseBytesCodecConfig(nlohmann::json config = nlohmann::json())
			{
				std::string endian = "little";
				for (auto it = config.begin(); it != config.end(); ++it)
				{
					if (it.key() == "endian")
					{
						endian = it.value();
						if (endian != "little" && endian != "big")
						{
							throw ITLException("Invalid endian in bytes codec config: " + endian);
						}
					}
					else
					{
						throw ITLException("Invalid key in bytes codec config: " + it.key());
					}
				}
				this->configuration = {
					{ "endian", endian }
				};
			}

			void getBloscConfiguration(string& cname, int& clevel, zarr::blosc::shuffle& shuffle, size_t& typesize, size_t& blocksize)
			{
				if (this->name != ZarrCodecName::Blosc) throw ITLException("only blosc codec has blosc config");
				try
				{
					cname = this->configuration["cname"];
					//todo: snappy?
					std::list<string> allowedCnames = { "blosclz", "lz4", "lz4hc", "zlib", "zstd" };
					if (!listContains<string>(allowedCnames, cname)) throw ITLException("invalid blosc cname: " + cname);
					clevel = this->configuration["clevel"];
					string shuffleName = this->configuration["shuffle"];
					if (shuffleName == "noshuffle") shuffle = blosc::shuffle::noshuffle;
					else if (shuffleName == "shuffle") shuffle = blosc::shuffle::shuffle;
					else if (shuffleName == "bitshuffle") shuffle = blosc::shuffle::bitshuffle;
					else throw ITLException("invalid blosc shuffle parameter: " + shuffleName);
					if (shuffle != blosc::shuffle::noshuffle)
					{
						if (this->configuration.contains("typesize"))
							typesize = this->configuration["typesize"];
						else if (typesize != 0)
							this->configuration["typesize"] = typesize;
						else throw ITLException("blosc error: no typesize set while shuffle!=noshuffle");
					}
					blocksize = this->configuration["blocksize"];
				}
				catch (nlohmann::json::exception ex)
				{
					throw ITLException("error in reading blosc config: " + nlohmann::to_string(this->configuration) + " got exception: " + ex.what());
				}
			}
			Vec3c transposeOrder()
			{
				try
				{
					if (this->name != ZarrCodecName::Transpose) throw ITLException("only transpose codec has transpose order");
					auto orderJSON = this->configuration["order"];
					//TODO check if valid order
					Vec3c order = Vec3c(0, 1, 2);
					order[0] = orderJSON[0].get<size_t>();
					if (orderJSON.size() >= 2)
						order[1] = orderJSON[1].get<size_t>();
					if (orderJSON.size() >= 3)
						order[2] = orderJSON[2].get<size_t>();
					cout << "transposeOrder=" << order << endl;
					return order;
				}
				catch (nlohmann::json::exception ex)
				{
					throw ITLException("error in reading transposeOrder of configuration: " + nlohmann::to_string(this->configuration) + " got exception: " + ex.what());
				}
			}
		};
		inline bool codecsFromJSON(std::list<ZarrCodec>& codecs, nlohmann::json codecsJSON, string& reason)
		{
			int numberArrayBytesCodecs = 0;
			for (auto& codec : codecsJSON)
			{
				if (!codec.contains("name"))
				{
					throw ITLException("codec name is missing in zarr metadata.");
				}
				try
				{
					nlohmann::json codecConfig = {};
					if (codec.contains("configuration"))
					{
						codecConfig = codec["configuration"];
					}
					zarr::ZarrCodecName zarrCodecName = fromString<zarr::ZarrCodecName>(codec["name"].get<string>());
					zarr::ZarrCodec zarrCodec = zarr::ZarrCodec(zarrCodecName, codecConfig);
					codecs.push_back(zarrCodec);
					switch (zarrCodec.type)
					{
					case ZarrCodecType::ArrayArrayCodec:
						if (numberArrayBytesCodecs > 0)
						{
							throw ITLException("ArrayArrayCodec cannot be used after ArrayBytesCodec.");
						}
						break;
					case ZarrCodecType::ArrayBytesCodec:
						numberArrayBytesCodecs++;
						break;
					case ZarrCodecType::BytesBytesCodec:
						if (numberArrayBytesCodecs < 1)
						{
							throw ITLException("ArrayBytesCodec must be used before BytesBytesCodec.");
						}
						break;
					default:
						reason = "Unknown codec type.";
						return false;
					}
				}
				catch (ITLException& e)
				{
					reason = e.message();
					return false;
				}
			}
			if (numberArrayBytesCodecs != 1)
			{
				throw ITLException("Exactly one ArrayBytesCodec was expected in the codecs list, got " + std::to_string(numberArrayBytesCodecs) + ".");
			}
			return true;
		}

		namespace internals
		{
			std::vector<char> readBytesOfFile(std::string filename)
			{
				std::ifstream ifs(filename, std::ios_base::binary | std::ios::ate);
				if (!ifs)
				{
					throw ITLException(std::string("Unable to open ") + filename + std::string(", ") + getStreamErrorMessage());
				}
				std::ifstream::pos_type pos = ifs.tellg();
				std::vector<char> result(pos);

				ifs.seekg(0, std::ios::beg);
				ifs.read(&result[0], pos);

				return result;
			}

			void writeBytesToFile(std::vector<char>& buffer, const std::string& filename, size_t startInFilePos = 0)
			{
				createFoldersFor(filename);
				size_t fileSize = buffer.size();
				setFileSize(filename, fileSize);
				std::ofstream out(filename.c_str(), std::ios_base::in | std::ios_base::out | std::ios_base::binary);
				if (!out)
					throw ITLException(std::string("Unable to open ") + filename + std::string(", ") + getStreamErrorMessage());
				out.write(&buffer[startInFilePos], fileSize - startInFilePos);
				if (!out)
					throw ITLException(std::string("Unable to write to ") + filename + std::string(", ") + getStreamErrorMessage());
			}

			template<typename pixel_t>
			void readBytesCodec(Image<pixel_t>& image, std::vector<char>& buffer)
			{
				Vec3c shape = image.dimensions();
				std::vector<pixel_t> temp(shape.product());
				std::memcpy(temp.data(), buffer.data(), buffer.size());

				assert(shape.product() * sizeof(pixel_t) == buffer.size());
				size_t n = 0;
				for (coord_t x = 0; x < shape.x; x++)
				{
					for (coord_t y = 0; y < shape.y; y++)
					{
						for (coord_t z = 0; z < shape.z; z++)
						{
							//todo: might be faster to read data sequentially
							image(x, y, z) = temp[n++];
							//cout << "set image(" << toString(Vec3c(x, y, z)) << ")=" << image(x, y, z) << endl;
						}
					}
				}
			}
			template<typename pixel_t>
			void writeBytesCodec(const Image<pixel_t>& image, std::vector<char>& buffer)
			{
				Vec3c shape = image.dimensions();
				std::vector<pixel_t> temp(shape.product());

				size_t n = 0;
				for (coord_t x = 0; x < shape.x; x++)
				{
					for (coord_t y = 0; y < shape.y; y++)
					{
						for (coord_t z = 0; z < shape.z; z++)
						{
							temp[n++] = image(x, y, z);
						}
					}
				}

				size_t bufferSize = shape.product()*sizeof(pixel_t);
				buffer = std::vector<char>(bufferSize);
				std::memcpy(buffer.data(), temp.data(), buffer.size());
			}
		}

	}
}