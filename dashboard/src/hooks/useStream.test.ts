import { describe, it, expect } from "vitest";
import {
  nalType,
  stripStartCode,
  toLengthPrefixed,
  buildAvcC,
  codecFromSps,
  readPtsUs,
  kTypeVideo,
  kTypeAudio,
  kFrameHeaderLen,
} from "./useStream";

describe("nalType", () => {
  it("reads the NAL type after a 4-byte start code", () => {
    // type 7 = SPS
    const data = new Uint8Array([0x00, 0x00, 0x00, 0x01, 0x67, 0xaa, 0xbb]);
    expect(nalType(data)).toBe(7);
  });

  it("reads the NAL type after a 3-byte start code", () => {
    // type 8 = PPS
    const data = new Uint8Array([0x00, 0x00, 0x01, 0x68, 0xaa]);
    expect(nalType(data)).toBe(8);
  });

  it("masks off nal_ref_idc bits, keeping only the 5-bit type", () => {
    // 0x65 = 0110 0101 -> nal_ref_idc=011, type=00101=5 (IDR slice)
    const data = new Uint8Array([0x00, 0x00, 0x00, 0x01, 0x65]);
    expect(nalType(data)).toBe(5);
  });

  it("returns null with no start code", () => {
    expect(nalType(new Uint8Array([0x01, 0x02, 0x03, 0x04]))).toBeNull();
  });

  it("returns null when the start code is the entire buffer", () => {
    expect(nalType(new Uint8Array([0x00, 0x00, 0x00, 0x01]))).toBeNull();
  });
});

describe("stripStartCode", () => {
  it("strips a 4-byte start code", () => {
    const data = new Uint8Array([0x00, 0x00, 0x00, 0x01, 0x67, 0x42]);
    expect(stripStartCode(data)).toEqual(new Uint8Array([0x67, 0x42]));
  });

  it("strips a 3-byte start code", () => {
    const data = new Uint8Array([0x00, 0x00, 0x01, 0x68, 0x11]);
    expect(stripStartCode(data)).toEqual(new Uint8Array([0x68, 0x11]));
  });

  it("returns the input unchanged when there is no start code", () => {
    const data = new Uint8Array([0x67, 0x42, 0x00]);
    expect(stripStartCode(data)).toEqual(data);
  });
});

describe("toLengthPrefixed", () => {
  it("prefixes the NAL with its 4-byte big-endian length", () => {
    const nal = new Uint8Array([0x65, 0x01, 0x02, 0x03]);
    const out = toLengthPrefixed(nal);
    expect(out).toEqual(new Uint8Array([0x00, 0x00, 0x00, 0x04, 0x65, 0x01, 0x02, 0x03]));
  });

  it("handles an empty NAL", () => {
    expect(toLengthPrefixed(new Uint8Array([]))).toEqual(new Uint8Array([0, 0, 0, 0]));
  });

  it("handles lengths spanning multiple length bytes", () => {
    const nal = new Uint8Array(300).fill(0xab);
    const out = toLengthPrefixed(nal);
    // 300 = 0x012C
    expect(out.slice(0, 4)).toEqual(new Uint8Array([0x00, 0x00, 0x01, 0x2c]));
    expect(out.length).toBe(304);
  });
});

describe("buildAvcC", () => {
  // Regression test: buildAvcC once stripped the SPS/PPS NAL header byte,
  // corrupting decoded width/height — avcC stores the full NAL (ISO 14496-15).
  it("embeds the full SPS/PPS NAL unit, header byte included", () => {
    const sps = new Uint8Array([0x67, 0x42, 0x00, 0x1e, 0xaa, 0xbb]);
    const pps = new Uint8Array([0x68, 0xce, 0x3c, 0x80]);
    const avcC = buildAvcC(sps, pps);

    // 11 fixed bytes + full sps + full pps (not sps.length - 1 / pps.length - 1)
    expect(avcC.length).toBe(11 + sps.length + pps.length);

    expect(avcC[0]).toBe(1); // configurationVersion
    expect(avcC[1]).toBe(sps[1]); // AVCProfileIndication
    expect(avcC[2]).toBe(sps[2]); // profile_compatibility
    expect(avcC[3]).toBe(sps[3]); // AVCLevelIndication
    expect(avcC[4]).toBe(0xff); // lengthSizeMinusOne=3 (4-byte lengths)
    expect(avcC[5]).toBe(0xe1); // numOfSequenceParameterSets=1

    const spsLen = (avcC[6] << 8) | avcC[7];
    expect(spsLen).toBe(sps.length);
    const embeddedSps = avcC.slice(8, 8 + spsLen);
    expect(embeddedSps).toEqual(sps); // full NAL, header byte (0x67) included

    let o = 8 + spsLen;
    expect(avcC[o]).toBe(1); // numOfPictureParameterSets
    o += 1;
    const ppsLen = (avcC[o] << 8) | avcC[o + 1];
    expect(ppsLen).toBe(pps.length);
    o += 2;
    const embeddedPps = avcC.slice(o, o + ppsLen);
    expect(embeddedPps).toEqual(pps); // full NAL, header byte (0x68) included
  });
});

describe("codecFromSps", () => {
  it("builds an avc1.PPCCLL codec string from profile/compat/level bytes", () => {
    const sps = new Uint8Array([0x67, 0x42, 0x00, 0x1e]);
    expect(codecFromSps(sps)).toBe("avc1.42001e");
  });

  it("pads single hex digits with a leading zero", () => {
    const sps = new Uint8Array([0x67, 0x0a, 0x00, 0x09]);
    expect(codecFromSps(sps)).toBe("avc1.0a0009");
  });
});

describe("readPtsUs", () => {
  // Wire format [type][pts_us][payload]: pts_us is 8-byte big-endian at offset 1.
  it("reads an 8-byte big-endian pts_us starting at offset 1", () => {
    const data = new Uint8Array(kFrameHeaderLen + 2);
    data[0] = kTypeVideo;
    // pts_us = 0x0000000000033333 (209715 decimal, arbitrary test value)
    const view = new DataView(data.buffer);
    view.setBigUint64(1, 0x33333n, false);
    expect(readPtsUs(data)).toBe(0x33333);
  });

  it("round-trips values the control plane actually sends (pts_us up to ~a few minutes)", () => {
    const ptsUs = 176_664_900; // observed real value, ~176s into a clip
    const data = new Uint8Array(kFrameHeaderLen);
    data[0] = kTypeAudio;
    new DataView(data.buffer).setBigUint64(1, BigInt(ptsUs), false);
    expect(readPtsUs(data)).toBe(ptsUs);
  });

  it("reads 0 correctly", () => {
    const data = new Uint8Array(kFrameHeaderLen);
    expect(readPtsUs(data)).toBe(0);
  });
});

describe("wire format type markers", () => {
  it("uses distinct, expected byte values for video and audio", () => {
    expect(kTypeVideo).toBe(0x00);
    expect(kTypeAudio).toBe(0x01);
    expect(kTypeVideo).not.toBe(kTypeAudio);
  });

  it("header length accounts for 1 type byte + 8 pts bytes", () => {
    expect(kFrameHeaderLen).toBe(9);
  });
});
