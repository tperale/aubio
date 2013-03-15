/*
  Copyright (C) 2006-2009 Paul Brossier <piem@aubio.org>

  This file is part of aubio.

  aubio is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  aubio is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with aubio.  If not, see <http://www.gnu.org/licenses/>.

*/

#include "aubio_priv.h"
#include "fvec.h"
#include "cvec.h"
#include "spectral/specdesc.h"
#include "spectral/phasevoc.h"
#include "onset/peakpicker.h"
#include "mathutils.h"
#include "onset/onset.h"

/** structure to store object state */
struct _aubio_onset_t {
  aubio_pvoc_t * pv;            /**< phase vocoder */
  aubio_specdesc_t * od;        /**< onset description function */
  aubio_peakpicker_t * pp;      /**< peak picker */
  cvec_t * fftgrain;            /**< phase vocoder output */
  fvec_t * of;                  /**< onset detection function */
  smpl_t threshold;             /**< onset peak picking threshold */
  smpl_t silence;               /**< silence threhsold */
  uint_t minioi;                /**< minimum inter onset interval */
  uint_t delay;                 /**< constant delay, in samples, removed from detected onset times */
  fvec_t * wasonset;            /**< number of blocks since last onset */
  uint_t samplerate;            /**< sampling rate of the input signal */
  uint_t hop_size;              /**< number of samples between two runs */

  uint_t total_frames;          /**< total number of frames processed since the beginning */
  uint_t last_onset;            /**< last detected onset location, in frames */
};

/* execute onset detection function on iput buffer */
void aubio_onset_do (aubio_onset_t *o, fvec_t * input, fvec_t * onset)
{
  smpl_t isonset = 0;
  smpl_t wasonset = 0;
  aubio_pvoc_do (o->pv,input, o->fftgrain);
  aubio_specdesc_do (o->od,o->fftgrain, o->of);
  aubio_peakpicker_do(o->pp, o->of, onset);
  isonset = onset->data[0];
  wasonset = o->wasonset->data[0];
  if (isonset > 0.) {
    if (aubio_silence_detection(input, o->silence)==1) {
      isonset  = 0;
      wasonset++;
    } else {
      if (wasonset > o->minioi) {
        wasonset = 0;
        o->last_onset = o->total_frames + isonset * o->hop_size;
      } else {
        isonset  = 0;
        wasonset++;
      }
    }
  } else {
    if (wasonset == -1 && aubio_silence_detection(input, o->silence) == 0) {
      //AUBIO_MSG("beginning of file is not silent, marking as onset\n",
      //  wasonset, aubio_silence_detection(input, o->silence));
      isonset = o->delay / o->hop_size;
      o->last_onset = o->delay;
      wasonset = 0;
    }
    wasonset++;
  }
  o->wasonset->data[0] = wasonset;
  //onset->data[0] = isonset * o->hop_size - o->delay;
  onset->data[0] = isonset;
  // also keep a copy of the offset for use in get_last_onset
  o->total_frames += o->hop_size;
  return;
}

smpl_t aubio_onset_get_last_onset (aubio_onset_t *o)
{
  return o->last_onset - o->delay;
}

smpl_t aubio_onset_get_last_onset_s (aubio_onset_t *o)
{
  return aubio_onset_get_last_onset (o) / (smpl_t) (o->samplerate);
}

smpl_t aubio_onset_get_last_onset_ms (aubio_onset_t *o)
{
  return aubio_onset_get_last_onset_s (o) / 1000.;
}

smpl_t aubio_onset_get_descriptor(aubio_onset_t * o) {
  return o->of->data[0];
}

smpl_t aubio_onset_get_thresholded_descriptor(aubio_onset_t * o) {
  fvec_t * thresholded = aubio_peakpicker_get_thresholded_input(o->pp);
  return thresholded->data[0];
}

uint_t aubio_onset_set_silence(aubio_onset_t * o, smpl_t silence) {
  o->silence = silence;
  return AUBIO_OK;
}

uint_t aubio_onset_set_threshold(aubio_onset_t * o, smpl_t threshold) {
  o->threshold = threshold;
  aubio_peakpicker_set_threshold(o->pp, o->threshold);
  return AUBIO_OK;
}

uint_t aubio_onset_set_minioi(aubio_onset_t * o, uint_t minioi) {
  o->minioi = FLOOR(minioi / 1000. * o->samplerate / o->hop_size);
  return AUBIO_OK;
}

uint_t aubio_onset_get_minioi(aubio_onset_t * o) {
  return o->minioi;
}

uint_t aubio_onset_set_delay(aubio_onset_t * o, uint_t delay) {
  o->delay = delay;
  return AUBIO_OK;
}

uint_t aubio_onset_get_delay(aubio_onset_t * o) {
  return o->delay;
}

uint_t aubio_onset_set_delay_s(aubio_onset_t * o, smpl_t delay) {
  return aubio_onset_set_delay (o, delay * o->samplerate);
}

smpl_t aubio_onset_get_delay_s(aubio_onset_t * o) {
  return aubio_onset_get_delay (o) / (smpl_t) o->samplerate;
}

uint_t aubio_onset_set_delay_ms(aubio_onset_t * o, smpl_t delay) {
  return aubio_onset_set_delay_s (o, delay / 1000.);
}

smpl_t aubio_onset_get_delay_ms(aubio_onset_t * o) {
  return aubio_onset_get_delay_s (o) * 1000.;
}

/* Allocate memory for an onset detection */
aubio_onset_t * new_aubio_onset (char_t * onset_mode, 
    uint_t buf_size, uint_t hop_size, uint_t samplerate)
{
  aubio_onset_t * o = AUBIO_NEW(aubio_onset_t);
  /** set some default parameter */
  o->last_onset = 0;
  o->threshold = 0.3;
  o->delay     = 4.3 * hop_size;
  o->minioi    = 5;
  o->silence   = -70;
  o->wasonset  = new_fvec(1);
  o->wasonset->data[0] = -1.;
  o->total_frames = 0;
  o->samplerate = samplerate;
  o->hop_size = hop_size;
  o->pv = new_aubio_pvoc(buf_size, hop_size);
  o->pp = new_aubio_peakpicker();
  aubio_peakpicker_set_threshold (o->pp, o->threshold);
  o->od = new_aubio_specdesc(onset_mode,buf_size);
  o->fftgrain = new_cvec(buf_size);
  o->of = new_fvec(1);
  /*if (usedoubled)    {
    o2 = new_aubio_specdesc(onset_type2,buffer_size);
    onset2 = new_fvec(1);
  }*/
  return o;
}

void del_aubio_onset (aubio_onset_t *o)
{
  del_aubio_specdesc(o->od);
  del_aubio_peakpicker(o->pp);
  del_aubio_pvoc(o->pv);
  del_fvec(o->of);
  del_fvec(o->wasonset);
  del_cvec(o->fftgrain);
  AUBIO_FREE(o);
}
