#include "lighting_dds.hpp"
#include <bit>
#include <fstream>
#include <stdexcept>

namespace renderkit
{
namespace
{
    std::uint16_t Half(float value)
    {
        if(!std::isfinite(value) || value<0 || value>65504)throw std::invalid_argument("Lighting value outside finite positive half-float range");
        if(value==0)return 0;
        const auto bits=std::bit_cast<std::uint32_t>(value);
        const int exponent=static_cast<int>((bits>>23)&255)-127;
        if(exponent<-25)return 0;
        const auto mantissa=bits&0x7fffffu;
        const unsigned shift=exponent<-14?static_cast<unsigned>(-exponent-1):13u;
        const auto significand=exponent<-14?(mantissa|0x800000u):mantissa;
        auto result=exponent<-14?(significand>>shift):
            (static_cast<std::uint32_t>(exponent+15)<<10)|(significand>>shift);
        const auto remainder=significand&((1u<<shift)-1),midpoint=1u<<(shift-1);
        if(remainder>midpoint || (remainder==midpoint && (result&1)))++result; // Round to nearest, ties to even.
        return static_cast<std::uint16_t>(result);
    }
}
void SaveLightingDds(const std::filesystem::path& path,int width,int height,
    std::span<const Vec3> colors,std::span<const std::uint8_t> rgba)
{
    if(width<1 || height<1 || width>4096 || height>4096 ||
       colors.size()!=static_cast<std::size_t>(width)*height || rgba.size()!=colors.size()*4)
        throw std::invalid_argument("Invalid lighting DDS dimensions");
    std::ofstream file(path,std::ios::binary|std::ios::trunc);
    if(!file)throw std::runtime_error("Cannot write lighting DDS: "+Utf8Path(path));
    std::array<std::uint32_t,37> header{};
    header[0]=0x20534444;header[1]=124;header[2]=0x100f; // DDS magic, size, CAPS|HEIGHT|WIDTH|PITCH|PIXELFORMAT.
    header[3]=static_cast<std::uint32_t>(height);header[4]=static_cast<std::uint32_t>(width);header[5]=static_cast<std::uint32_t>(width)*8;
    header[19]=32;header[20]=4;header[21]=0x30315844; // Pixel format: FOURCC 'DX10'.
    header[27]=0x1000; // DDSCAPS_TEXTURE.
    header[32]=10;header[33]=3;header[35]=1;header[36]=2; // R16G16B16A16_FLOAT, TEXTURE2D, array=1, PREMULTIPLIED alpha.
    for(auto word:header)for(int b=0;b<4;++b)file.put(static_cast<char>((word>>(b*8))&255));
    std::vector<char> row(static_cast<std::size_t>(width)*8);
    for(int y=0;y<height;++y)
    {
        for(int x=0;x<width;++x)
        {
            const auto pixel=static_cast<std::size_t>(y)*width+x;
            for(int c=0;c<4;++c)
            {
                const float alpha=rgba[pixel*4+3]/255.0f;
                const auto half=Half(c<3?Component(colors[pixel],c)*alpha:alpha);
                row[static_cast<std::size_t>(x)*8+c*2]=static_cast<char>(half&255);
                row[static_cast<std::size_t>(x)*8+c*2+1]=static_cast<char>(half>>8);
            }
        }
        file.write(row.data(),static_cast<std::streamsize>(row.size()));
    }
    file.close();if(!file)throw std::runtime_error("Failed to finish lighting DDS: "+Utf8Path(path));
}
}
