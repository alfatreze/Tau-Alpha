/* Real-file metadata: enough to drive vu_master's technical-info overlay (fw/vu_master.inc's vum_draw_overlay, spec section 5)
 * from an actual dropped MP3/FLAC file instead of the synthetic demo caption. Everything here is read-only parsing of the file's
 * own bytes -- no network, no external library (this lab's CSP only allows scripts from a short CDN allowlist and this build is
 * one inline file, so a dependency-free parser matches the rest of the stack).
 *
 * The MP3 LAME/Xing encoder-tag search mirrors fw/player.c's vbr_frame_count() (around line 8084): same 'Xing'/'Info' ASCII
 * search, same flag-driven offset math (FRAMES +4, BYTES +4, TOC +100, QUALITY +4) to reach the 9-byte encoder string, same
 * printable-bytes guard and trailing-space strip -- replicated rather than re-derived so this can't quietly disagree with the
 * firmware's own reading of the same bytes. track_encoder's FLAC branch ("FLAC <bps>-bit", fw/player.c around line 8586) is
 * copied the same way for the FLAC case. */
(function (root) {
  function findId3v2Size(u8) {
    if (u8.length < 10 || u8[0] !== 0x49 || u8[1] !== 0x44 || u8[2] !== 0x33) return 0;   // 'ID3'
    const sz = ((u8[6] & 0x7f) << 21) | ((u8[7] & 0x7f) << 14) | ((u8[8] & 0x7f) << 7) | (u8[9] & 0x7f);
    return 10 + sz;
  }

  /* Minimal ID3v2.3/2.4 text-frame reader (TIT2/TPE1/TALB) -- a bonus for the overlay, not load-bearing for format/rate/bitrate. */
  function readId3v2Text(u8) {
    const out = {};
    if (u8.length < 10 || u8[0] !== 0x49 || u8[1] !== 0x44 || u8[2] !== 0x33) return out;
    const ver = u8[3], size = findId3v2Size(u8);
    let p = 10;
    const wantId3 = { TIT2: 'title', TPE1: 'artist', TALB: 'album' };
    while (p + 10 <= size && p + 10 <= u8.length) {
      const id = String.fromCharCode(u8[p], u8[p + 1], u8[p + 2], u8[p + 3]);
      if (id === '\0\0\0\0') break;
      let flen;
      if (ver >= 4) flen = ((u8[p + 4] & 0x7f) << 21) | ((u8[p + 5] & 0x7f) << 14) | ((u8[p + 6] & 0x7f) << 7) | (u8[p + 7] & 0x7f);
      else flen = (u8[p + 4] << 24) | (u8[p + 5] << 16) | (u8[p + 6] << 8) | u8[p + 7];
      const body = p + 10;
      if (flen < 1 || body + flen > u8.length) break;
      if (wantId3[id]) {
        const enc = u8[body];
        let s;
        if (enc === 1 || enc === 2) {   // UTF-16 (with or without BOM) -- decode as UTF-16LE, good enough for a lab overlay
          const bytes = u8.subarray(body + 1, body + flen);
          let str = '';
          for (let i = (bytes[0] === 0xff || bytes[0] === 0xfe) ? 2 : 0; i + 1 < bytes.length; i += 2) str += String.fromCharCode(bytes[i] | (bytes[i + 1] << 8));
          s = str;
        } else {
          s = ''; for (let i = body + 1; i < body + flen; i++) if (u8[i]) s += String.fromCharCode(u8[i]);
        }
        out[wantId3[id]] = s.replace(/\0+$/, '').trim();
      }
      p = body + flen;
    }
    return out;
  }

  /* First MPEG-1/2/2.5 Layer III frame header: exact bitrate/sample-rate for a CBR file, and a plausible first-frame bitrate for
   * a VBR one (the average is computed from file size/duration instead -- see fileMeta() below). */
  const MPEG1_L3_KBPS = [0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0];
  const MPEG2_L3_KBPS = [0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0];
  const RATES = { 3: [44100, 48000, 32000], 2: [22050, 24000, 16000], 0: [11025, 12000, 8000] };   // keyed by the 2-bit version id (MPEG2.5 = 0)
  function findMp3FrameHeader(u8, from) {
    const lim = Math.min(u8.length - 4, from + 65536);
    for (let i = from; i < lim; i++) {
      if (u8[i] !== 0xff || (u8[i + 1] & 0xe0) !== 0xe0) continue;
      const verId = (u8[i + 1] >> 3) & 3, layer = (u8[i + 1] >> 1) & 3;
      if (layer !== 1) continue;                                // Layer III only
      const brIdx = (u8[i + 2] >> 4) & 0x0f, srIdx = (u8[i + 2] >> 2) & 3;
      if (brIdx === 0 || brIdx === 15 || srIdx === 3) continue;
      const kbps = (verId === 3 ? MPEG1_L3_KBPS : MPEG2_L3_KBPS)[brIdx];
      const hz = RATES[verId][srIdx];
      if (!kbps || !hz) continue;
      const channels = ((u8[i + 3] >> 6) & 3) === 3 ? 1 : 2;
      return { offset: i, kbps, hz, channels, mpeg2: verId !== 3 };
    }
    return null;
  }

  function findVbrTag(u8, audioStart) {
    const lim = Math.min(u8.length, audioStart + 2048);
    for (let i = audioStart; i + 20 < lim; i++) {
      const a = u8[i], b = u8[i + 1], c = u8[i + 2], d = u8[i + 3];
      /* 'Xing' means a genuinely variable bitrate; LAME (and others) also write 'Info' in exactly the same slot for a CBR file,
       * purely to carry a seek table -- its own bitrate is still constant and exact from the frame header, so the two are NOT
       * the same thing for the "is this kbps figure an estimate" question even though the byte layout that follows is identical. */
      const isXing = a === 0x58 && b === 0x69 && c === 0x6e && d === 0x67;
      const isInfo = a === 0x49 && b === 0x6e && c === 0x66 && d === 0x6f;
      const isVbri = a === 0x56 && b === 0x42 && c === 0x52 && d === 0x49;   // 'VBRI'
      if (!isXing && !isInfo && !isVbri) continue;
      if (isVbri) return { vbr: true, encoder: null };   // VBRI carries no encoder tag; presence alone marks the file as VBR
      const flags = (u8[i + 4] << 24) | (u8[i + 5] << 16) | (u8[i + 6] << 8) | u8[i + 7];
      let e = i + 8;
      if (flags & 1) e += 4;    // FRAMES
      if (flags & 2) e += 4;    // BYTES
      if (flags & 4) e += 100;  // TOC
      if (flags & 8) e += 4;    // QUALITY
      let encoder = null;
      if (e + 10 < lim) {
        let ok = true;
        for (let k = 0; k < 9; k++) if (u8[e + k] < 0x20 || u8[e + k] > 0x7e) { ok = false; break; }
        if (ok) {
          let s = '';
          for (let k = 0; k < 9; k++) s += String.fromCharCode(u8[e + k]);
          encoder = s.replace(/ +$/, '');
        }
      }
      return { vbr: isXing, encoder: encoder || null };
    }
    return null;
  }

  function mp3Meta(u8, fileSize, durationSec) {
    const id3 = readId3v2Text(u8), audioStart = findId3v2Size(u8);
    const fh = findMp3FrameHeader(u8, audioStart);
    const vbr = findVbrTag(u8, audioStart);
    const out = { format: 'MP3', sampleRate: fh ? fh.hz : null, channels: fh ? fh.channels : null, encoder: (vbr && vbr.encoder) || null, tags: id3 };
    if (vbr && vbr.vbr) {
      out.kbps = durationSec > 0 ? Math.round((fileSize * 8) / durationSec / 1000) : (fh ? fh.kbps : null);
      out.kbpsEstimated = true;   // true VBR ('Xing'/'VBRI'): only a file-size/duration average is meaningful
    } else if (fh) {
      out.kbps = fh.kbps; out.kbpsEstimated = false;    // CBR (no tag, or an 'Info' seek-table tag): the frame header states the exact rate
    } else {
      out.kbps = durationSec > 0 ? Math.round((fileSize * 8) / durationSec / 1000) : null;
      out.kbpsEstimated = true;
    }
    return out;
  }

  function u32be(u8, p) { return ((u8[p] << 24) | (u8[p + 1] << 16) | (u8[p + 2] << 8) | u8[p + 3]) >>> 0; }
  function u32le(u8, p) { return ((u8[p + 3] << 24) | (u8[p + 2] << 16) | (u8[p + 1] << 8) | u8[p]) >>> 0; }
  function bits(u8, startBit, n) {   // big-endian bit reader over a byte range; safe up to 36 bits (< 2^53)
    let v = 0;
    for (let i = 0; i < n; i++) {
      const bp = startBit + i, byteIdx = bp >> 3, bitIdx = 7 - (bp & 7);
      v = v * 2 + ((u8[byteIdx] >> bitIdx) & 1);
    }
    return v;
  }

  function flacMeta(u8, fileSize) {
    const start = findId3v2Size(u8);   // FLAC files don't normally carry an ID3v2 tag, but some taggers add one anyway
    if (u8[start] !== 0x66 || u8[start + 1] !== 0x4c || u8[start + 2] !== 0x61 || u8[start + 3] !== 0x43) return null;   // 'fLaC'
    let p = start + 4, out = { format: 'FLAC', tags: {} };
    for (let guard = 0; guard < 64 && p + 4 <= u8.length; guard++) {
      const last = (u8[p] & 0x80) !== 0, type = u8[p] & 0x7f, len = (u8[p + 1] << 16) | (u8[p + 2] << 8) | u8[p + 3];
      const body = p + 4;
      if (body + len > u8.length) break;
      if (type === 0 && len >= 18) {   // STREAMINFO
        out.sampleRate = bits(u8, (body + 10) * 8, 20);
        out.channels = bits(u8, (body + 10) * 8 + 20, 3) + 1;
        out.bitsPerSample = bits(u8, (body + 10) * 8 + 23, 5) + 1;
        out.totalSamples = bits(u8, (body + 10) * 8 + 28, 36);
      } else if (type === 4) {   // VORBIS_COMMENT
        let q = body;
        const vlen = u32le(u8, q); q += 4 + vlen;
        if (q + 4 <= body + len) {
          const nComments = u32le(u8, q); q += 4;
          for (let i = 0; i < nComments && q + 4 <= body + len; i++) {
            const clen = u32le(u8, q); q += 4;
            if (q + clen > body + len) break;
            let s = ''; for (let k = 0; k < clen; k++) s += String.fromCharCode(u8[q + k]);
            q += clen;
            const eq = s.indexOf('=');
            if (eq > 0) {
              const key = s.slice(0, eq).toUpperCase(), val = s.slice(eq + 1);
              if (key === 'TITLE') out.tags.title = val;
              else if (key === 'ARTIST') out.tags.artist = val;
              else if (key === 'ALBUM') out.tags.album = val;
            }
          }
        }
      }
      p = body + len;
      if (last) break;
    }
    if (out.sampleRate && out.totalSamples) {
      const secs = out.totalSamples / out.sampleRate;
      out.kbps = secs > 0 ? Math.round((fileSize * 8) / secs / 1000) : null;
      out.kbpsEstimated = true;   // FLAC has no per-frame rate; this is the whole-file average, same convention as track_kbps's FLAC branch
    }
    out.encoder = out.bitsPerSample ? ('FLAC ' + out.bitsPerSample + '-bit') : 'FLAC';   // mirrors fw/player.c's track_encoder format-row text for FLAC
    return out;
  }

  /* Top-level entry: bytes = the whole file as a Uint8Array, fileSize/durationSec from the caller (duration comes from
   * AudioBuffer.duration post-decode, which is exact and free -- no need to derive it from tags). */
  function fileMeta(bytes, fileSize, durationSec) {
    const u8 = bytes instanceof Uint8Array ? bytes : new Uint8Array(bytes);
    if (u8.length >= 4 && u8[0] === 0x66 && u8[1] === 0x4c && u8[2] === 0x61 && u8[3] === 0x43) return flacMeta(u8, fileSize);
    const id3size = findId3v2Size(u8);
    if (u8.length > id3size + 4 && u8[id3size] === 0x66 && u8[id3size + 1] === 0x4c && u8[id3size + 2] === 0x61 && u8[id3size + 3] === 0x43) return flacMeta(u8, fileSize);
    return mp3Meta(u8, fileSize, durationSec);   // default: everything else that isn't FLAC is treated as MP3 (the lab only offers these two)
  }

  const api = { fileMeta, findId3v2Size, readId3v2Text, findMp3FrameHeader, findVbrTag, flacMeta, mp3Meta };
  if (typeof module !== 'undefined') module.exports = api; else root.TauTags = api;
})(typeof globalThis !== 'undefined' ? globalThis : window);
