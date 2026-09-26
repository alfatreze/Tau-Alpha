/* ***** BEGIN LICENSE BLOCK ***** 
 * Version: RCSL 1.0/RPSL 1.0 
 *  
 * Portions Copyright (c) 1995-2002 RealNetworks, Inc. All Rights Reserved. 
 *      
 * The contents of this file, and the files included with this file, are 
 * subject to the current version of the RealNetworks Public Source License 
 * Version 1.0 (the "RPSL") available at 
 * http://www.helixcommunity.org/content/rpsl unless you have licensed 
 * the file under the RealNetworks Community Source License Version 1.0 
 * (the "RCSL") available at http://www.helixcommunity.org/content/rcsl, 
 * in which case the RCSL will apply. You may also obtain the license terms 
 * directly from RealNetworks.  You may not use this file except in 
 * compliance with the RPSL or, if you have a valid RCSL with RealNetworks 
 * applicable to this file, the RCSL.  Please see the applicable RPSL or 
 * RCSL for the rights, obligations and limitations governing use of the 
 * contents of the file.  
 *  
 * This file is part of the Helix DNA Technology. RealNetworks is the 
 * developer of the Original Code and owns the copyrights in the portions 
 * it created. 
 *  
 * This file, and the files included with this file, is distributed and made 
 * available on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER 
 * EXPRESS OR IMPLIED, AND REALNETWORKS HEREBY DISCLAIMS ALL SUCH WARRANTIES, 
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY, FITNESS 
 * FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT. 
 * 
 * Technology Compatibility Kit Test Suite(s) Location: 
 *    http://www.helixcommunity.org/content/tck 
 * 
 * Contributor(s): 
 *  
 * ***** END LICENSE BLOCK ***** */ 

/**************************************************************************************
 * Fixed-point MP3 decoder
 * Jon Recker (jrecker@real.com), Ken Cooke (kenc@real.com)
 * June 2003
 *
 * subband.c - subband transform (synthesis filterbank implemented via 32-point DCT
 *               followed by polyphase filter)
 **************************************************************************************/

#include "coder.h"
#include "assembly.h"

/* TAU_POLY_FW (B-307): redirect the stereo branch's FDCT32 output to the MP3 window unit (tau_mp3_poly.sv, TAU_POLY) via
 * fw/mp3_poly_hw.h/.inc, instead of calling PolyphaseStereo. Off by default -- byte-identical to the unmodified decoder.
 * FDCT32 still always runs unconditionally (it updates sbi->vbuf, which the per-slot software fallback below needs, and
 * which the mono path -- never redirected -- always needs); only the window computation itself is optionally redirected.
 * dct32.c's own TAU_POLY_FW hook is what makes tau_poly_wlog[]/tau_poly_wn available here. */
#ifndef TAU_POLY_FW
#define TAU_POLY_FW 0
#endif
#if TAU_POLY_FW
#include "mp3_poly_hw.h"
extern int tau_poly_wlog[33];
extern int tau_poly_wn;
#endif

/**************************************************************************************
 * Function:    Subband
 *
 * Description: do subband transform on all the blocks in one granule, all channels
 *
 * Inputs:      filled MP3DecInfo structure, after calling IMDCT for all channels
 *              vbuf[ch] and vindex[ch] must be preserved between calls
 *
 * Outputs:     decoded PCM data, interleaved LRLRLR... if stereo
 *
 * Return:      0 on success,  -1 if null input pointers
 **************************************************************************************/
int Subband(MP3DecInfo *mp3DecInfo, short *pcmBuf)
{
	int b;
	HuffmanInfo *hi;
	IMDCTInfo *mi;
	SubbandInfo *sbi;

	/* validate pointers */
	if (!mp3DecInfo || !mp3DecInfo->HuffmanInfoPS || !mp3DecInfo->IMDCTInfoPS || !mp3DecInfo->SubbandInfoPS)
		return -1;

	hi = (HuffmanInfo *)mp3DecInfo->HuffmanInfoPS;
	mi = (IMDCTInfo *)(mp3DecInfo->IMDCTInfoPS);
	sbi = (SubbandInfo*)(mp3DecInfo->SubbandInfoPS);

	if (mp3DecInfo->nChans == 2) {
		/* stereo */
#if TAU_POLY_FW
		int hw_this_track = tau_poly_hw_enable;
		if (hw_this_track && !sbi->hwPolyReady) {
			tau_poly_hw_clear();
			hw_this_track = tau_poly_hw_enable;   /* tau_poly_hw_clear() may have just disabled it */
			sbi->hwPolyReady = 1;                  /* either way: don't ask again for this decoder instance */
		}
#endif
		for (b = 0; b < BLOCK_SIZE; b++) {
#if TAU_POLY_FW
			if (hw_this_track) {
				int w0[32], w1[32];
				tau_poly_wn = 0;
				FDCT32(mi->outBuf[0][b], sbi->vbuf + 0*32, sbi->vindex, (b & 0x01), mi->gb[0]);
				for (int k = 0, j = 0; k < 33; k++) if (k != 17) w0[j++] = tau_poly_wlog[k];
				tau_poly_wn = 0;
				FDCT32(mi->outBuf[1][b], sbi->vbuf + 1*32, sbi->vindex, (b & 0x01), mi->gb[1]);
				for (int k = 0, j = 0; k < 33; k++) if (k != 17) w1[j++] = tau_poly_wlog[k];
				if (!tau_poly_hw_slot(w0, w1, pcmBuf)) {
					hw_this_track = 0;              /* this slot's redirect failed: finish the track in software */
					PolyphaseStereo(pcmBuf, sbi->vbuf + sbi->vindex + VBUF_LENGTH * (b & 0x01), polyCoef);
				} else if (tau_poly_verify_left > 0) {
					/* self-check the first slots of every decoder instance against the real window (vbuf is always current, FDCT32 above updated it) */
					short chk[2 * NBANDS];
					tau_poly_verify_left--;
					PolyphaseStereo(chk, sbi->vbuf + sbi->vindex + VBUF_LENGTH * (b & 0x01), polyCoef);
					for (int k = 0; k < 2 * NBANDS; k++) if (chk[k] != pcmBuf[k]) {
						tau_poly_stat_mismatch++;
						tau_poly_hw_enable = 0;     /* a unit that disagrees with the software window is not trusted again this session */
						hw_this_track = 0;
						for (int j = 0; j < 2 * NBANDS; j++) pcmBuf[j] = chk[j];
						break;
					}
				}
			} else
#endif
			{
				FDCT32(mi->outBuf[0][b], sbi->vbuf + 0*32, sbi->vindex, (b & 0x01), mi->gb[0]);
				FDCT32(mi->outBuf[1][b], sbi->vbuf + 1*32, sbi->vindex, (b & 0x01), mi->gb[1]);
				PolyphaseStereo(pcmBuf, sbi->vbuf + sbi->vindex + VBUF_LENGTH * (b & 0x01), polyCoef);
			}
			sbi->vindex = (sbi->vindex - (b & 0x01)) & 7;
			pcmBuf += (2 * NBANDS);
		}
	} else {
		/* mono */
		for (b = 0; b < BLOCK_SIZE; b++) {
			FDCT32(mi->outBuf[0][b], sbi->vbuf + 0*32, sbi->vindex, (b & 0x01), mi->gb[0]);
			PolyphaseMono(pcmBuf, sbi->vbuf + sbi->vindex + VBUF_LENGTH * (b & 0x01), polyCoef);
			sbi->vindex = (sbi->vindex - (b & 0x01)) & 7;
			pcmBuf += NBANDS;
		}
	}

	return 0;
}

