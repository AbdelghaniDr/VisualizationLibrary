/**************************************************************************************/
/*                                                                                    */
/*  Visualization Library                                                             */
/*  http://www.visualizationlibrary.com                                               */
/*                                                                                    */
/*  Copyright (c) 2005-2010, Michele Bosi                                             */
/*  All rights reserved.                                                              */
/*                                                                                    */
/*  Redistribution and use in source and binary forms, with or without modification,  */
/*  are permitted provided that the following conditions are met:                     */
/*                                                                                    */
/*  - Redistributions of source code must retain the above copyright notice, this     */
/*  list of conditions and the following disclaimer.                                  */
/*                                                                                    */
/*  - Redistributions in binary form must reproduce the above copyright notice, this  */
/*  list of conditions and the following disclaimer in the documentation and/or       */
/*  other materials provided with the distribution.                                   */
/*                                                                                    */
/*  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND   */
/*  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED     */
/*  WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE            */
/*  DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR  */
/*  ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES    */
/*  (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;      */
/*  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON    */
/*  ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT           */
/*  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS     */
/*  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.                      */
/*                                                                                    */
/**************************************************************************************/

#include <vl/VirtualFile.hpp>
#include <vl/Log.hpp>
#include <vl/Say.hpp>
#include <vl/CRC32CheckSum.hpp>

using namespace vl;

//-----------------------------------------------------------------------------
unsigned int VirtualFile::crc32()
{
  unsigned int sum = 0;

  if ( open(OM_ReadOnly) )
  {
    CRC32CheckSum check_sum;
    sum = check_sum.compute(this);
    close();
  }

  return sum;
}
//-----------------------------------------------------------------------------
MD5CheckSum VirtualFile::md5()
{
  MD5CheckSum check_sum;
  if ( open(OM_ReadOnly) )
  {
    check_sum.compute(this);
    close();
  }
  return check_sum;
}
//-----------------------------------------------------------------------------
long long VirtualFile::peek(void* buffer, long long byte_count)
{
  if ( !isOpen() )
  {
    Log::error("VirtualFile::peek(): the file is closed.\n");
    return 0;
  }
  long long pos = position();
  long long read_bytes = read(buffer, byte_count);
  if ( !seekSet( pos ) )
    Log::error("VirtualFile::peek() called on a non seek-able VirtualFile.\n");
  return read_bytes;
}
//-----------------------------------------------------------------------------
long long VirtualFile::read(void* buffer, long long byte_count)
{
  if (byte_count > 0)
    return read_Implementation(buffer, byte_count);
  else
    return 0;
}
//-----------------------------------------------------------------------------
long long VirtualFile::write(const void* buffer, long long byte_count)
{
  if (byte_count > 0)
    return write_Implementation(buffer, byte_count);
  else
    return 0;
}
//-----------------------------------------------------------------------------
long long VirtualFile::position() const
{
  return position_Implementation();
}
//-----------------------------------------------------------------------------
bool VirtualFile::seekSet(long long offset)
{
  if (offset < 0)
  {
    Log::error( Say("VirtualFile::seekSet(%n): invalid offset.\n") << offset);
    seekSet_Implementation(0);
    return false;
  }
  if (offset > size() )
  {
    Log::error( Say("VirtualFile::seekSet(%n): invalid offset past end of stream.\n") << offset);
    seekSet_Implementation(size());
    return false;
  }

  return seekSet_Implementation(offset);
}
//-----------------------------------------------------------------------------
bool VirtualFile::seekCur(long long offset)
{
  return seekSet( position() + offset );
}
//-----------------------------------------------------------------------------
bool VirtualFile::seekEnd(long long offset)
{
  return seekSet( size() + offset );
}
//-----------------------------------------------------------------------------
long long VirtualFile::load(std::vector<unsigned char>& data)
{
  data.resize( (size_t)size() );
  if (data.size())
    return load(&data[0], data.size());
  else
    return 0;
}
//-----------------------------------------------------------------------------
long long VirtualFile::load(void* buffer, long long max)
{
  if (max<0)
    max = size();
  if ( open(OM_ReadOnly) )
  {
    long long bytes = read(buffer,max);
    close();
    return bytes;
  }
  else
  {
    Log::error( Say("Cannot load file '%s'.\n") << path() );
    return 0;
  }
}
//-----------------------------------------------------------------------------
float VirtualFile::readFloat(bool little_endian_data)
{
  union {
    float num;
    unsigned char bytes[4];
  } data;

  read(data.bytes, 4);

  unsigned short bet = 0x00FF;
  bool little_endian_cpu = ((unsigned char*)&bet)[0] == 0xFF;
  if ( little_endian_cpu != little_endian_data )
  {
    // swap the bytes
    unsigned char tmp;
    tmp = data.bytes[0]; data.bytes[0] = data.bytes[3]; data.bytes[3] = tmp;
    tmp = data.bytes[1]; data.bytes[1] = data.bytes[2]; data.bytes[2] = tmp;
  }

  return data.num;
}
//-----------------------------------------------------------------------------
long long VirtualFile::readFloat(float* buffer, long long count, bool little_endian_data)
{
  return readSInt32((long*)buffer, sizeof(float)*count, little_endian_data);
}
//-----------------------------------------------------------------------------
double VirtualFile::readDouble(bool little_endian_data)
{
  union {
    double num;
    unsigned char bytes[8];
  } data;
  VL_COMPILE_TIME_CHECK( sizeof(data.num) == 8 )

  read(data.bytes, 8);

  unsigned short bet = 0x00FF;
  bool little_endian_cpu = ((unsigned char*)&bet)[0] == 0xFF;
  if ( little_endian_cpu != little_endian_data )
  {
    // swap the bytes
    unsigned char tmp;
    tmp = data.bytes[0]; data.bytes[0] = data.bytes[7]; data.bytes[7] = tmp;
    tmp = data.bytes[1]; data.bytes[1] = data.bytes[6]; data.bytes[6] = tmp;
    tmp = data.bytes[2]; data.bytes[2] = data.bytes[5]; data.bytes[5] = tmp;
    tmp = data.bytes[3]; data.bytes[3] = data.bytes[4]; data.bytes[4] = tmp;
  }

  return data.num;
}
//-----------------------------------------------------------------------------
long long VirtualFile::readDouble(double* buffer, long long count, bool little_endian_data)
{
  return readSInt64((long long*)buffer, sizeof(double)*count, little_endian_data);
}
//-----------------------------------------------------------------------------
unsigned char VirtualFile::readUInt8()
{
  unsigned char ch = 0;
  read(&ch,sizeof(unsigned char));
  return ch;
}
//-----------------------------------------------------------------------------
long long VirtualFile::readUInt8(unsigned char* buffer, long long count)
{
  return read(buffer, sizeof(unsigned char)*count);
}
//-----------------------------------------------------------------------------
char VirtualFile::readSInt8()
{
  char ch = 0;
  read(&ch,sizeof(char));
  return ch;
}
//-----------------------------------------------------------------------------
long long VirtualFile::readSInt8(char* buffer, long long count)
{
  return read((unsigned char*)buffer, sizeof(char)*count);
}
//-----------------------------------------------------------------------------
unsigned long long VirtualFile::readUInt64(bool little_endian_data)
{
  unsigned long long num;
  char* bytes = (char*)&num;

  VL_COMPILE_TIME_CHECK( sizeof(num) == 8 )

  read(bytes, 8);

  unsigned short bet = 0x00FF;
  bool little_endian_cpu = ((unsigned char*)&bet)[0] == 0xFF;
  if ( little_endian_cpu != little_endian_data )
  {
    // swap the bytes
    unsigned char tmp;
    tmp = bytes[0]; bytes[0] = bytes[7]; bytes[7] = tmp;
    tmp = bytes[1]; bytes[1] = bytes[6]; bytes[6] = tmp;
    tmp = bytes[2]; bytes[2] = bytes[5]; bytes[5] = tmp;
    tmp = bytes[3]; bytes[3] = bytes[4]; bytes[4] = tmp;
  }

  return num;
}
//-----------------------------------------------------------------------------
long long VirtualFile::readSInt64(bool little_endian_data)
{
  long long num;
  char* bytes = (char*)&num;

  VL_COMPILE_TIME_CHECK( sizeof(num) == 8 )

    read(bytes, 8);

  unsigned short bet = 0x00FF;
  bool little_endian_cpu = ((unsigned char*)&bet)[0] == 0xFF;
  if ( little_endian_cpu != little_endian_data )
  {
    // swap the bytes
    unsigned char tmp;
    tmp = bytes[0]; bytes[0] = bytes[7]; bytes[7] = tmp;
    tmp = bytes[1]; bytes[1] = bytes[6]; bytes[6] = tmp;
    tmp = bytes[2]; bytes[2] = bytes[5]; bytes[5] = tmp;
    tmp = bytes[3]; bytes[3] = bytes[4]; bytes[4] = tmp;
  }

  return num;
}
//-----------------------------------------------------------------------------
unsigned long VirtualFile::readUInt32(bool little_endian_data)
{
  union {
    unsigned int num;
    unsigned char bytes[4];
  } data;
  VL_COMPILE_TIME_CHECK( sizeof(data.num) == 4 )

  read(data.bytes, 4);

  unsigned short bet = 0x00FF;
  bool little_endian_cpu = ((unsigned char*)&bet)[0] == 0xFF;
  if ( little_endian_cpu != little_endian_data )
  {
    // swap the bytes
    unsigned char tmp;
    tmp = data.bytes[0]; data.bytes[0] = data.bytes[3]; data.bytes[3] = tmp;
    tmp = data.bytes[1]; data.bytes[1] = data.bytes[2]; data.bytes[2] = tmp;
  }

  return data.num;
}
//-----------------------------------------------------------------------------
long VirtualFile::readSInt32(bool little_endian_data)
{
  union {
    int num;
    unsigned char bytes[4];
  } data;
  VL_COMPILE_TIME_CHECK( sizeof(data.num) == 4 )

  read(data.bytes, 4);

  unsigned short bet = 0x00FF;
  bool little_endian_cpu = ((unsigned char*)&bet)[0] == 0xFF;
  if ( little_endian_cpu != little_endian_data )
  {
    // swap the bytes
    unsigned char tmp;
    tmp = data.bytes[0]; data.bytes[0] = data.bytes[3]; data.bytes[3] = tmp;
    tmp = data.bytes[1]; data.bytes[1] = data.bytes[2]; data.bytes[2] = tmp;
  }

  return data.num;
}

//-----------------------------------------------------------------------------
unsigned short VirtualFile::readUInt16(bool little_endian_data)
{
  union {
    unsigned short num;
    unsigned char bytes[2];
  } data;
  VL_COMPILE_TIME_CHECK( sizeof(data.num) == 2 )

  read(data.bytes, 2);

  unsigned short bet = 0x00FF;
  bool little_endian_cpu = ((unsigned char*)&bet)[0] == 0xFF;
  if ( little_endian_cpu != little_endian_data )
  {
    // swap the bytes
    unsigned char tmp;
    tmp = data.bytes[0]; data.bytes[0] = data.bytes[1]; data.bytes[1] = tmp;
  }

  return data.num;
}
//-----------------------------------------------------------------------------
short VirtualFile::readSInt16(bool little_endian_data)
{
  union {
    short num;
    unsigned char bytes[2];
  } data;
  VL_COMPILE_TIME_CHECK( sizeof(data.num) == 2 )

  read(data.bytes, 2);

  unsigned short bet = 0x00FF;
  bool little_endian_cpu = ((unsigned char*)&bet)[0] == 0xFF;
  if ( little_endian_cpu != little_endian_data )
  {
    // swap the bytes
    unsigned char tmp;
    tmp = data.bytes[0]; data.bytes[0] = data.bytes[1]; data.bytes[1] = tmp;
  }

  return data.num;
}
//-----------------------------------------------------------------------------
long long VirtualFile::readUInt64(unsigned long long* buffer, long long count, bool little_endian_data)
{
  // read
  long long c = read(buffer, count*sizeof(unsigned long long));

  // convert endianess
  unsigned short bet = 0x00FF;
  bool little_endian_cpu = ((unsigned char*)&bet)[0] == 0xFF;
  if ( little_endian_cpu != little_endian_data )
  {
    unsigned char* ch = (unsigned char*)buffer;
    for(int i=0; i<count*8; i+=8)
    {
      // swap bytes
      unsigned char tmp;
      tmp = ch[i+0];
      ch[i+0] = ch[i+7];
      ch[i+7] = tmp;
      tmp = ch[i+1];
      ch[i+1] = ch[i+6];
      ch[i+6] = tmp;
      tmp = ch[i+2];
      ch[i+2] = ch[i+5];
      ch[i+5] = tmp;
      tmp = ch[i+3];
      ch[i+3] = ch[i+4];
      ch[i+4] = tmp;
    }
  }

  return c;
}
//-----------------------------------------------------------------------------
long long VirtualFile::readSInt64(long long* buffer, long long count, bool little_endian_data)
{
  return readUInt64((unsigned long long*)buffer,count,little_endian_data);
}
//-----------------------------------------------------------------------------
long long VirtualFile::readUInt32(unsigned long* buffer, long long count, bool little_endian_data)
{
  // read
  long long c = read(buffer, count*sizeof(unsigned int));

  // convert endianess
  unsigned short bet = 0x00FF;
  bool little_endian_cpu = ((unsigned char*)&bet)[0] == 0xFF;
  if ( little_endian_cpu != little_endian_data )
  {
    unsigned char* ch = (unsigned char*)buffer;
    for(int i=0; i<count*4; i+=4)
    {
      // swap bytes
      unsigned char tmp;
      tmp = ch[i+0];
      ch[i+0] = ch[i+3];
      ch[i+3] = tmp;
      tmp = ch[i+1];
      ch[i+1] = ch[i+2];
      ch[i+2] = tmp;
    }
  }

  return c;
}
//-----------------------------------------------------------------------------
long long VirtualFile::readSInt32(long* buffer, long long count, bool little_endian_data)
{
  return readUInt32((unsigned long*)buffer,count,little_endian_data);
}
//-----------------------------------------------------------------------------
long long VirtualFile::readUInt16(unsigned short* buffer, long long count, bool little_endian_data)
{
  // read
  long long c = read(buffer, count*sizeof(unsigned short));

  // convert endianess
  unsigned short bet = 0x00FF;
  bool little_endian_cpu = ((unsigned char*)&bet)[0] == 0xFF;
  if ( little_endian_cpu != little_endian_data )
  {
    unsigned char* ch = (unsigned char*)buffer;
    for(int i=0; i<count*2; i+=2)
    {
      // swap bytes
      unsigned char tmp = ch[i+0];
      ch[i+0] = ch[i+1];
      ch[i+1] = tmp;
    }
  }

  return c;
}
//-----------------------------------------------------------------------------
long long VirtualFile::readSInt16(short* buffer, long long count, bool little_endian_data)
{
  return readUInt16((unsigned short*)buffer,count,little_endian_data);
}
//-----------------------------------------------------------------------------
long long VirtualFile::writeUInt32(unsigned long data, bool little_endian)
{
  unsigned short bet = 0x00FF;
  bool little_endian_cpu = ((unsigned char*)&bet)[0] == 0xFF;
  char* byte = (char*)&data;
  if (little_endian_cpu != little_endian)
  {
    char tmp = byte[0];
    byte[0] = byte[3];
    byte[3] = tmp;
    tmp = byte[1];
    byte[1] = byte[2];
    byte[2] = tmp;
  }
  return write(byte, sizeof(data));
}
//-----------------------------------------------------------------------------
long long VirtualFile::writeSInt32(long data, bool little_endian)
{
  unsigned short bet = 0x00FF;
  bool little_endian_cpu = ((unsigned char*)&bet)[0] == 0xFF;
  char* byte = (char*)&data;
  if (little_endian_cpu != little_endian)
  {
    char tmp = byte[0];
    byte[0] = byte[3];
    byte[3] = tmp;
    tmp = byte[1];
    byte[1] = byte[2];
    byte[2] = tmp;
  }
  return write(byte, sizeof(data));
}
//-----------------------------------------------------------------------------
long long VirtualFile::writeUInt16(unsigned short data, bool little_endian)
{
  unsigned short bet = 0x00FF;
  bool little_endian_cpu = ((unsigned char*)&bet)[0] == 0xFF;
  char* byte = (char*)&data;
  if (little_endian_cpu != little_endian)
  {
    char tmp = byte[0];
    byte[0] = byte[1];
    byte[1] = tmp;
  }
  return write(byte, sizeof(data));
}
//-----------------------------------------------------------------------------
long long VirtualFile::writeSInt16(short data, bool little_endian)
{
  unsigned short bet = 0x00FF;
  bool little_endian_cpu = ((unsigned char*)&bet)[0] == 0xFF;
  char* byte = (char*)&data;
  if (little_endian_cpu != little_endian)
  {
    char tmp = byte[0];
    byte[0] = byte[1];
    byte[1] = tmp;
  }
  return write(byte, sizeof(data));
}
//-----------------------------------------------------------------------------
long long VirtualFile::writeUInt8(unsigned char data)
{
  return write(&data,1);
}
//-----------------------------------------------------------------------------
long long VirtualFile::writeSInt8(char data)
{
  return write(&data,1);
}
//-----------------------------------------------------------------------------
long long VirtualFile::writeSInt64(long long data, bool little_endian)
{
  unsigned short bet = 0x00FF;
  bool little_endian_cpu = ((unsigned char*)&bet)[0] == 0xFF;
  char* byte = (char*)&data;
  if (little_endian_cpu != little_endian)
  {
    // swap the bytes
    unsigned char tmp;
    char* bytes = (char*)&data;
    tmp = bytes[0]; bytes[0] = bytes[7]; bytes[7] = tmp;
    tmp = bytes[1]; bytes[1] = bytes[6]; bytes[6] = tmp;
    tmp = bytes[2]; bytes[2] = bytes[5]; bytes[5] = tmp;
    tmp = bytes[3]; bytes[3] = bytes[4]; bytes[4] = tmp;
  }
  return write(byte, sizeof(data));
}
//-----------------------------------------------------------------------------
long long VirtualFile::writeUInt64(unsigned long long data, bool little_endian)
{
  unsigned short bet = 0x00FF;
  bool little_endian_cpu = ((unsigned char*)&bet)[0] == 0xFF;
  char* byte = (char*)&data;
  if (little_endian_cpu != little_endian)
  {
    // swap the bytes
    unsigned char tmp;
    char* bytes = (char*)&data;
    tmp = bytes[0]; bytes[0] = bytes[7]; bytes[7] = tmp;
    tmp = bytes[1]; bytes[1] = bytes[6]; bytes[6] = tmp;
    tmp = bytes[2]; bytes[2] = bytes[5]; bytes[5] = tmp;
    tmp = bytes[3]; bytes[3] = bytes[4]; bytes[4] = tmp;
  }
  return write(byte, sizeof(data));
}
//-----------------------------------------------------------------------------
long long VirtualFile::writeFloat(float data, bool little_endian)
{
  unsigned short bet = 0x00FF;
  bool little_endian_cpu = ((unsigned char*)&bet)[0] == 0xFF;
  char* byte = (char*)&data;
  if (little_endian_cpu != little_endian)
  {
    char tmp = byte[0];
    byte[0] = byte[3];
    byte[3] = tmp;
    tmp = byte[1];
    byte[1] = byte[2];
    byte[2] = tmp;
  }
  return write(byte, sizeof(data));
}
//-----------------------------------------------------------------------------
long long VirtualFile::writeDouble(double data, bool little_endian)
{
  unsigned short bet = 0x00FF;
  bool little_endian_cpu = ((unsigned char*)&bet)[0] == 0xFF;
  char* byte = (char*)&data;
  if (little_endian_cpu != little_endian)
  {
    // swap the bytes
    unsigned char tmp;
    char* bytes = (char*)&data;
    tmp = bytes[0]; bytes[0] = bytes[7]; bytes[7] = tmp;
    tmp = bytes[1]; bytes[1] = bytes[6]; bytes[6] = tmp;
    tmp = bytes[2]; bytes[2] = bytes[5]; bytes[5] = tmp;
    tmp = bytes[3]; bytes[3] = bytes[4]; bytes[4] = tmp;
  }
  return write(byte, sizeof(data));
}
//-----------------------------------------------------------------------------
long long VirtualFile::writeDouble(double* buffer, long long count, bool little_endian_data)
{
  unsigned short bet = 0x00FF;
  bool little_endian_cpu = ((unsigned char*)&bet)[0] == 0xFF;
  if (little_endian_cpu != little_endian_data)
  {
    long long ret = 0;
    for(long long i=0; i<count; ++i)
      ret += writeDouble( buffer[i], little_endian_data );
    return ret;
  }
  else
    return write(buffer,sizeof(buffer[0])*count);
}
//-----------------------------------------------------------------------------
long long VirtualFile::writeFloat(float* buffer, long long count, bool little_endian_data)
{
  unsigned short bet = 0x00FF;
  bool little_endian_cpu = ((unsigned char*)&bet)[0] == 0xFF;
  if (little_endian_cpu != little_endian_data)
  {
    long long ret = 0;
    for(long long i=0; i<count; ++i)
      ret += writeFloat( buffer[i], little_endian_data );
    return ret;
  }
  else
    return write(buffer,sizeof(buffer[0])*count);
}
//-----------------------------------------------------------------------------
long long VirtualFile::writeUInt64(unsigned long long* buffer, long long count, bool little_endian_data)
{
  unsigned short bet = 0x00FF;
  bool little_endian_cpu = ((unsigned char*)&bet)[0] == 0xFF;
  if (little_endian_cpu != little_endian_data)
  {
    long long ret = 0;
    for(long long i=0; i<count; ++i)
      ret += writeUInt64( buffer[i], little_endian_data );
    return ret;
  }
  else
    return write(buffer,sizeof(buffer[0])*count);
}
//-----------------------------------------------------------------------------
long long VirtualFile::writeSInt64(long long* buffer, long long count, bool little_endian_data)
{
  unsigned short bet = 0x00FF;
  bool little_endian_cpu = ((unsigned char*)&bet)[0] == 0xFF;
  if (little_endian_cpu != little_endian_data)
  {
    long long ret = 0;
    for(long long i=0; i<count; ++i)
      ret += writeSInt64( buffer[i], little_endian_data );
    return ret;
  }
  else
    return write(buffer,sizeof(buffer[0])*count);
}
//-----------------------------------------------------------------------------
long long VirtualFile::writeUInt32(unsigned long* buffer, long long count, bool little_endian_data)
{
  unsigned short bet = 0x00FF;
  bool little_endian_cpu = ((unsigned char*)&bet)[0] == 0xFF;
  if (little_endian_cpu != little_endian_data)
  {
    long long ret = 0;
    for(long long i=0; i<count; ++i)
      ret += writeUInt32( buffer[i], little_endian_data );
    return ret;
  }
  else
    return write(buffer,sizeof(buffer[0])*count);
}
//-----------------------------------------------------------------------------
long long VirtualFile::writeSInt32(long* buffer, long long count, bool little_endian_data)
{
  unsigned short bet = 0x00FF;
  bool little_endian_cpu = ((unsigned char*)&bet)[0] == 0xFF;
  if (little_endian_cpu != little_endian_data)
  {
    long long ret = 0;
    for(long long i=0; i<count; ++i)
      ret += writeSInt32( buffer[i], little_endian_data );
    return ret;
  }
  else
    return write(buffer,sizeof(buffer[0])*count);
}
//-----------------------------------------------------------------------------
long long VirtualFile::writeUInt16(unsigned short* buffer, long long count, bool little_endian_data)
{
  unsigned short bet = 0x00FF;
  bool little_endian_cpu = ((unsigned char*)&bet)[0] == 0xFF;
  if (little_endian_cpu != little_endian_data)
  {
    long long ret = 0;
    for(long long i=0; i<count; ++i)
      ret += writeUInt16( buffer[i], little_endian_data );
    return ret;
  }
  else
    return write(buffer,sizeof(buffer[0])*count);
}
//-----------------------------------------------------------------------------
long long VirtualFile::writeSInt16(short* buffer, long long count, bool little_endian_data)
{
  unsigned short bet = 0x00FF;
  bool little_endian_cpu = ((unsigned char*)&bet)[0] == 0xFF;
  if (little_endian_cpu != little_endian_data)
  {
    long long ret = 0;
    for(long long i=0; i<count; ++i)
      ret += writeSInt16( buffer[i], little_endian_data );
    return ret;
  }
  else
    return write(buffer,sizeof(buffer[0])*count);
}
//-----------------------------------------------------------------------------
long long VirtualFile::writeUInt8(unsigned char* buffer, long long count)
{
  return write(buffer, count);
}
//-----------------------------------------------------------------------------
long long VirtualFile::writeSInt8(char* buffer, long long count)
{
  return write(buffer, count);
}
//-----------------------------------------------------------------------------
// mic fixme todo: 1) fai test completo, 2) ordina funzioni, 3) minimizza implementazione funzioni, 4) documenta funzioni nuove & tutto virtual file