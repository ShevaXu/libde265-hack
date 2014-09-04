/*
 * libde265 example application "sherlock265".
 * Copyright (c) 2013-2014 struktur AG, Dirk Farin <farin@struktur.de>
 *
 * This file is part of sherlock265, an example application using libde265.
 *
 * sherlock265 is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * sherlock265 is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with sherlock265.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "VideoDecoder.hh"
#ifdef HAVE_VIDEOGFX
#include <libvideogfx.hh>
#endif


#ifdef HAVE_VIDEOGFX
using namespace videogfx;
#endif

//#include "decctx.h"
#include "visualize.h"


VideoDecoder::VideoDecoder()
  : ctx(NULL),
    img(NULL),
    mNextBuffer(0),
    mFrameCount(0),
    mPlayingVideo(false),
    mVideoEnded(false),
    mSingleStep(false),
    mShowDecodedImage(true),
    mCBShowPartitioning(false),
    mTBShowPartitioning(false),
    mPBShowPartitioning(false),
    mShowPBPredMode(false),
    mShowIntraPredMode(false),
    mShowQuantPY(false),
    mShowSlices(false),
    mShowTiles(false),
    mShowSao(false),
    mFH(NULL),
    // new
    mOutputF(NULL)
#ifdef HAVE_SWSCALE
    , sws(NULL)
    , width(0)
    , height(0)
#endif
{
}


VideoDecoder::~VideoDecoder()
{
  free_decoder();
#ifdef HAVE_SWSCALE
  if (sws != NULL) {
    sws_freeContext(sws);
  }
#endif
}

void VideoDecoder::run()
{
  decoder_loop();
}


void VideoDecoder::init(const char* filename)
{
  init_decoder(filename);
}


void VideoDecoder::startDecoder()
{
  if (mPlayingVideo || mVideoEnded) { return; }

  mPlayingVideo=true;
  exit();
}

void VideoDecoder::stopDecoder()
{
  if (!mPlayingVideo) { return; }

  mPlayingVideo=false;
}

void VideoDecoder::singleStepDecoder()
{
  if (mPlayingVideo || mVideoEnded) { return; }

  mPlayingVideo=true;
  mSingleStep=true;
  exit();
}

void VideoDecoder::decoder_loop()
{
  for (;;)
    {
      if (mPlayingVideo) {
        mutex.lock();

        if (img) {
          img = NULL;
          de265_release_next_picture(ctx);
        }

        img = de265_peek_next_picture(ctx);
        while (img==NULL)
          {
            mutex.unlock();
            int more=1;
            de265_error err = de265_decode(ctx, &more);
            mutex.lock();

            if (more && err == DE265_OK) {
              // try again to get picture

              img = de265_peek_next_picture(ctx);
            }
            else if (more && err == DE265_ERROR_WAITING_FOR_INPUT_DATA) {
              uint8_t buf[4096];
              int buf_size = fread(buf,1,sizeof(buf),mFH);
              int err = de265_push_data(ctx,buf,buf_size ,0,0);
            }
            else if (!more)
              {
                mVideoEnded=true;
                mPlayingVideo=false; // TODO: send signal back
                break;
              }
          }


        // show one decoded picture

        if (img) {
          show_frame(img);

          if (mSingleStep) {
            mSingleStep=false;
            mPlayingVideo=false;
          }
        }

        mutex.unlock();

        // process events

        QCoreApplication::processEvents();
      }
      else {
        exec();
      }
    }
}

#ifdef HAVE_VIDEOGFX
void VideoDecoder::convert_frame_libvideogfx(const de265_image* img, QImage & qimg)
{
  // --- convert to RGB ---

  Image<Pixel> visu;
  visu.Create(img->get_width(), img->get_height(), Colorspace_YUV, Chroma_420);

  for (int y=0;y<img->get_height(0);y++) {
    memcpy(visu.AskFrameY()[y], img->get_image_plane_at_pos(0, 0,y), img->get_width(0));
  }

  for (int y=0;y<img->get_height(1);y++) {
    memcpy(visu.AskFrameU()[y], img->get_image_plane_at_pos(1, 0,y), img->get_width(1));
  }

  for (int y=0;y<img->get_height(2);y++) {
    memcpy(visu.AskFrameV()[y], img->get_image_plane_at_pos(2, 0,y), img->get_width(2));
  }

  Image<Pixel> debugvisu;
  ChangeColorspace(debugvisu, visu, Colorspace_RGB);

  // --- convert to QImage ---

  uchar* ptr = qimg.bits();
  int bpl = qimg.bytesPerLine();

  for (int y=0;y<img->get_height();y++)
    {
      for (int x=0;x<img->get_width();x++)
        {
          *(uint32_t*)(ptr+x*4) = ((debugvisu.AskFrameR()[y][x] << 16) |
                                   (debugvisu.AskFrameG()[y][x] <<  8) |
                                   (debugvisu.AskFrameB()[y][x] <<  0));
        }

      ptr += bpl;
    }
}
#endif

#ifdef HAVE_SWSCALE
void VideoDecoder::convert_frame_swscale(const de265_image* img, QImage & qimg)
{
  if (sws == NULL || img->get_width() != width || img->get_height() != height) {
    if (sws != NULL) {
      sws_freeContext(sws);
    }
    width = img->get_width();
    height = img->get_height();
    sws = sws_getContext(width, height, PIX_FMT_YUV420P, width, height, PIX_FMT_BGRA, SWS_FAST_BILINEAR, NULL, NULL, NULL);
  }

  int stride[3];
  const uint8_t *data[3];
  for (int c=0;c<3;c++) {
    data[c]   = img->get_image_plane(c);
    stride[c] = img->get_image_stride(c);
  }

  uint8_t *qdata[1] = { (uint8_t *) qimg.bits() };
  int qstride[1] = { qimg.bytesPerLine() };
  sws_scale(sws, data, stride, 0, img->get_height(), qdata, qstride);
}
#endif

void VideoDecoder::show_frame(const de265_image* img)
{
  if (mFrameCount==0) {
    mImgBuffers[0] = QImage(QSize(img->get_width(),img->get_height()), QImage::Format_RGB32);
    mImgBuffers[1] = QImage(QSize(img->get_width(),img->get_height()), QImage::Format_RGB32);
  }

  // --- convert to RGB (or generate a black image if video image is disabled) ---

  QImage* qimg = &mImgBuffers[mNextBuffer];
  uchar* ptr = qimg->bits();
  int bpl = qimg->bytesPerLine();

  if (mShowDecodedImage) {
#ifdef HAVE_VIDEOGFX
    convert_frame_libvideogfx(img, *qimg);
#elif HAVE_SWSCALE
    convert_frame_swscale(img, *qimg);
#else
    qimg->fill(QColor(0, 0, 0));
#endif
  } else {
    qimg->fill(QColor(0, 0, 0));
  }

  // --- overlay coding-mode visualization ---

    
  if (mShowQuantPY)
    {
      draw_QuantPY(img, ptr, bpl, 4);
    }

  if (mShowPBPredMode)
    {
      draw_PB_pred_modes(img, ptr, bpl, 4);
    }

  if (mShowIntraPredMode)
    {
      draw_intra_pred_modes(img, ptr, bpl, 0x009090ff, 4);
    }

  if (mTBShowPartitioning)
    {
      draw_TB_grid(img, ptr, bpl, 0x00ff6000, 4);
    }

  if (mPBShowPartitioning)
    {
      draw_PB_grid(img, ptr, bpl, 0x00e000, 4);
    }

  if (mCBShowPartitioning)
    {
      draw_CB_grid(img, ptr, bpl, 0x00FFFFFF, 4);
    }

  if (mShowMotionVec)
    {
      draw_Motion(img, ptr, bpl, 4);
    }

  if (mShowSlices)
    {
      draw_Slices(img, ptr, bpl, 4);
    }

  if (mShowTiles)
    {
      draw_Tiles(img, ptr, bpl, 4);
    }
    
    // new
    if (mShowSao) {
        draw_custom_sao_info(img, ptr, bpl, 4);
    }

  emit displayImage(qimg);
  mNextBuffer = 1-mNextBuffer;
  mFrameCount++;
}


void VideoDecoder::showCBPartitioning(bool flag)
{
  mCBShowPartitioning=flag;

  mutex.lock();
  if (img != NULL) { show_frame(img); }
  mutex.unlock();
}


void VideoDecoder::showTBPartitioning(bool flag)
{
  mTBShowPartitioning=flag;

  mutex.lock();
  if (img != NULL) { show_frame(img); }
  mutex.unlock();
}

void VideoDecoder::showPBPartitioning(bool flag)
{
  mPBShowPartitioning=flag;

  mutex.lock();
  if (img != NULL) { show_frame(img); }
  mutex.unlock();
}

void VideoDecoder::showIntraPredMode(bool flag)
{
  mShowIntraPredMode=flag;

  mutex.lock();
  if (img != NULL) { show_frame(img); }
  mutex.unlock();
}

void VideoDecoder::showPBPredMode(bool flag)
{
  mShowPBPredMode=flag;

  mutex.lock();
  if (img != NULL) { show_frame(img); }
  mutex.unlock();
}

void VideoDecoder::showQuantPY(bool flag)
{
  mShowQuantPY=flag;

  mutex.lock();
  if (img != NULL) { show_frame(img); }
  mutex.unlock();
}

void VideoDecoder::showMotionVec(bool flag)
{
  mShowMotionVec=flag;

  mutex.lock();
  if (img != NULL) { show_frame(img); }
  mutex.unlock();
}

void VideoDecoder::showDecodedImage(bool flag)
{
  mShowDecodedImage=flag;

  mutex.lock();
  if (img != NULL) { show_frame(img); }
  mutex.unlock();
}

void VideoDecoder::showTiles(bool flag)
{
  mShowTiles=flag;

  mutex.lock();
  if (img != NULL) { show_frame(img); }
  mutex.unlock();
}

void VideoDecoder::showSlices(bool flag)
{
  mShowSlices=flag;

  mutex.lock();
  if (img != NULL) { show_frame(img); }
  mutex.unlock();
}

//new
void VideoDecoder::showSaoInfo(bool flag)
{
    mShowSao = flag;
    
    mutex.lock();
    if (img != NULL) { show_frame(img); }
    mutex.unlock();
}

void tint_rect(uint8_t* img, int stride, int x0,int y0,int w,int h, uint32_t color, int pixelSize)
{
    for (int y=0;y<h;y++)
        for (int x=0;x<w;x++)
        {
            int xp = x0+x;
            int yp = y0+y;
            
            for (int i=0;i<pixelSize;i++) {
                uint8_t col = (color>>(i*8)) & 0xFF;
                img[yp*stride+xp*pixelSize + i] = (img[yp*stride+xp*pixelSize + i] + col)/2;
            }
        }
}

void VideoDecoder::draw_custom_sao_info(const de265_image* img, uint8_t* dst, int stride, int pixelSize)
{
    // TODO
    
    // red, blue, green
    // yellow for special value
    // here for EO with offset abs < 1
    uint32_t cols[4] = { 0xff0000, 0x0000ff, 0x00ff00, 0xffff00 };
    
    int sliceType = img->slices[0]->slice_type;
    
    int CtbsH = img->sps.PicHeightInCtbsY;
    int CtbsW = img->sps.PicWidthInCtbsY;
    int plH = img->get_height();
    int plW = img->get_width();
    int CtbSize = img->sps.CtbSizeY;
    int compSize = CtbSize / 3; // for Y, U and V
    
    for (int yCtb = 0; yCtb < CtbsH; yCtb++)
    {
        for (int xCtb = 0; xCtb < CtbsW; xCtb++)
        {
            const sao_info* saoInfo = img->get_sao_info(xCtb, yCtb);
            int x0 = xCtb * CtbSize;
            int y0 = yCtb * CtbSize;
            
            // TODO: deal with x out of range
            if (xCtb == CtbsW - 1 && x0 + CtbSize > plW) {
                continue;
            }
            
            // deal with y out of range
            int drawH = (yCtb == CtbsH - 1 && y0 + CtbSize > plH)? plH - y0: CtbSize;
            
            // loop for 3 channels
            for (int cIdx = 0; cIdx < 3; cIdx++) {
                int SaoTypeIdx = (saoInfo->SaoTypeIdx >> (2*cIdx)) & 0x3;
                int SaoEoClass = (saoInfo->SaoEoClass >> (2*cIdx)) & 0x3;
                // draw with different color
                uint32_t col = cols[SaoTypeIdx];
                
#if 0
                if (SaoTypeIdx == 2) {
                    // EO
                    int8_t offsets[4];
                    offsets[0] = abs(saoInfo->saoOffsetVal[cIdx][0]);
                    offsets[1] = abs(saoInfo->saoOffsetVal[cIdx][1]);
                    offsets[2] = abs(saoInfo->saoOffsetVal[cIdx][2]);
                    offsets[3] = abs(saoInfo->saoOffsetVal[cIdx][3]);
                    // all abs < 1
                    if (offsets[0] <= 1 && offsets[1] <= 1 && offsets[2] <= 1 && offsets[3] <= 1) {
                        col = cols[3];
                    }
                }
#endif
                
                tint_rect(dst, stride, x0 + cIdx * compSize, y0, compSize, drawH, col, pixelSize);
                
                // log
                if (SaoTypeIdx && mOutputF)
                    fprintf(mOutputF, "%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n",
                            mFrameCount, sliceType,
                            SaoTypeIdx, cIdx, SaoEoClass,
                            saoInfo->saoOffsetVal[cIdx][0],
                            saoInfo->saoOffsetVal[cIdx][1],
                            saoInfo->saoOffsetVal[cIdx][2],
                            saoInfo->saoOffsetVal[cIdx][3]);
            }
        }
    }
    //QTextStream(stdout) << "hello sao world " << CtbsW << "x" << CtbsH << "x" << CtbSize << "\t" << plW << "x" << plH << endl;
}



void VideoDecoder::init_decoder(const char* filename)
{
  mFH = fopen(filename,"rb");
  //init_file_context(&inputctx, filename);
  //rbsp_buffer_init(&buf);
    
    mOutputF = fopen("output_sao.txt", "w");

  ctx = de265_new_decoder();
  de265_start_worker_threads(ctx, 4); // start 4 background threads
}

void VideoDecoder::free_decoder()
{
  if (mFH) { fclose(mFH); }
    
    if (mOutputF) {
        fclose(mOutputF);
    }

  if (ctx) { de265_free_decoder(ctx); }
}
