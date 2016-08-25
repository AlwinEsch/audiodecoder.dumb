/*
 *      Copyright (C) 2014 Arne Morten Kvarving
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#include <vector>

#include <kodi/api2/Addon.hpp>
#include <kodi/api2/addon/VFSUtils.hpp>
#include <kodi/api2/audiodecoder/Addon.hpp>

extern "C" {
#include <dumb.h>
}

using namespace AudioDecoder;

struct dumbfile_mem_status
{
  const uint8_t * ptr;
  unsigned offset, size;

  dumbfile_mem_status() : offset(0), size(0), ptr(nullptr) {}

  ~dumbfile_mem_status()
  {
    delete[] ptr;
  }
};

static int dumbfile_mem_skip(void * f, long n)
{
  dumbfile_mem_status * s = (dumbfile_mem_status *) f;
  s->offset += n;
  if (s->offset > s->size)
  {
    s->offset = s->size;
    return 1;
  }

  return 0;
}

static int dumbfile_mem_getc(void * f)
{
  dumbfile_mem_status * s = (dumbfile_mem_status *) f;
  if (s->offset < s->size)
  {
    return *(s->ptr + s->offset++);
  }
  return -1;
}

static long dumbfile_mem_getnc(char * ptr, long n, void * f)
{
  dumbfile_mem_status * s = (dumbfile_mem_status *) f;
  long max = s->size - s->offset;
  if (max > n) max = n;
  if (max)
  {
    memcpy(ptr, s->ptr + s->offset, max);
    s->offset += max;
  }
  return max;
}

static int dumbfile_mem_seek(void * f, long n)
{
  dumbfile_mem_status * s = (dumbfile_mem_status *) f;
  if ( n < 0 || n > s->size ) return -1;
  s->offset = n;
  return 0;
}

static long dumbfile_mem_get_size(void * f)
{
  dumbfile_mem_status * s = (dumbfile_mem_status *) f;
  return s->size;
}

static DUMBFILE_SYSTEM mem_dfs = {
  nullptr, // open
  &dumbfile_mem_skip,
  &dumbfile_mem_getc,
  &dumbfile_mem_getnc,
  nullptr // close
};

struct DumbContext
{
  DUH* module;
  DUH_SIGRENDERER* sr;
};

class CDumbCodec : AudioDecoder::CAddonInterface
{
public:
  CDumbCodec(void* kodiInstance) : CAddonInterface(kodiInstance), m_dumb(nullptr) { }
  virtual ~CDumbCodec() { }

  virtual bool Init(std::string file, unsigned int filecache,
                    int& channels, int& samplerate,
                    int& bitspersample, int64_t& totaltime,
                    int& bitrate, eAudioDataFormat& format,
                    std::vector<eAudioChannel>& channellist) override;
  virtual int ReadPCM(uint8_t* buffer, int size, int* actualsize) override;
  virtual bool DeInit() override;
 
private:
  DumbContext* m_dumb;
};

bool CDumbCodec::Init(std::string filename, unsigned int filecache,
                       int& channels, int& samplerate,
                       int& bitspersample, int64_t& totaltime,
                       int& bitrate, eAudioDataFormat& format,
                       std::vector<eAudioChannel>& channellist)
{
  AddOn::CVFSFile file;
  if (!file.OpenFile(filename, 0))
    return false;

  dumbfile_mem_status memdata;
  memdata.size = file.GetLength();
  memdata.ptr = new uint8_t[memdata.size];
  file.Read(const_cast<uint8_t*>(memdata.ptr), memdata.size);
  file.Close();

  DUMBFILE* f = dumbfile_open_ex(&memdata, &mem_dfs);
  if (!f)
    return false;

  m_dumb = new DumbContext;

  if (memdata.size >= 4 &&
      memdata.ptr[0] == 'I' && memdata.ptr[1] == 'M' &&
      memdata.ptr[2] == 'P' && memdata.ptr[3] == 'M')
  {
    m_dumb->module = dumb_read_it(f);
  }
  else if (memdata.size >= 17 &&
           memcmp(memdata.ptr, "Extended Module: ", 17) == 0)
  {
    m_dumb->module = dumb_read_xm(f);
  }
  else if (memdata.size >= 0x30 &&
           memdata.ptr[0x2C] == 'S' && memdata.ptr[0x2D] == 'C' &&
           memdata.ptr[0x2E] == 'R' && memdata.ptr[0x2F] == 'M')
  {
    m_dumb->module = dumb_read_s3m(f);
  }
  else
  {
    dumbfile_close(f);
    delete m_dumb;
    m_dumb = nullptr;
    return false;
  }

  dumbfile_close(f);

  m_dumb->sr = duh_start_sigrenderer(m_dumb->module, 0, 2, 0);

  if (!m_dumb->sr)
  {
    delete m_dumb;
    m_dumb = nullptr;
    return false;
  }

  channels = 2;
  samplerate = 48000;
  bitspersample = 16;
  totaltime = duh_get_length(m_dumb->module)/65536*1000;
  format = AUDIO_FMT_S16NE;
  channellist = { AUDIO_CH_FL, AUDIO_CH_FR, AUDIO_CH_NULL };
  bitrate = duh_sigrenderer_get_n_channels(m_dumb->sr);

  return true;
}

int CDumbCodec::ReadPCM(uint8_t* buffer, int size, int* actualsize)
{
  if (!m_dumb)
    return 1;

  int rendered = duh_render(m_dumb->sr, 16, 0, 1.0,
                            65536.0/48000.0,
                            size/4, buffer);
  *actualsize = rendered*4;

  return 0;
}

bool CDumbCodec::DeInit()
{
  if (m_dumb)
  {
    duh_end_sigrenderer(m_dumb->sr);
    unload_duh(m_dumb->module);
    delete m_dumb;
    m_dumb = nullptr;
    return true;
  }

  return false;
}

//------------------------------------------------------------------------------
//==============================================================================
//------------------------------------------------------------------------------

class CAddonDumbCodec : public CAddon
{
public:
  CAddonDumbCodec() { }

  virtual eAddonStatus CreateInstance(eInstanceType instanceType, KODI_HANDLE* addonInstance, KODI_HANDLE kodiInstance) override;
  virtual void DestroyInstance(KODI_HANDLE addonInstance) override;
};

eAddonStatus CAddonDumbCodec::CreateInstance(eInstanceType instanceType, KODI_HANDLE* addonInstance, KODI_HANDLE kodiInstance)
{
  KodiAPI::Log(ADDON_LOG_DEBUG, "%s - Creating the Dumb audio decoder add-on", __FUNCTION__);

  if (instanceType != CAddon::instanceAudioDecoder)
  {
    KodiAPI::Log(ADDON_LOG_FATAL, "%s - Creation called with unsupported instance type", __FUNCTION__);
    return addonStatus_PERMANENT_FAILURE;
  }

  *addonInstance = new CDumbCodec(kodiInstance);
  return addonStatus_OK;
}

void CAddonDumbCodec::DestroyInstance(KODI_HANDLE addonInstance)
{
  delete static_cast<CDumbCodec*>(addonInstance);
}

ADDONCREATOR(CAddonDumbCodec); // Don't touch this!
