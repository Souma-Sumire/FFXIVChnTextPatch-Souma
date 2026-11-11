package zipatch

import (
	"bytes"
	"compress/flate"
	"context"
	"encoding/binary"
	"fmt"
	"hash/crc32"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"testing"
)

func buildChunk(fourcc string, body []byte) []byte {
	var buf bytes.Buffer
	_ = binary.Write(&buf, binary.BigEndian, uint32(len(body)))
	buf.WriteString(fourcc)
	buf.Write(body)
	h := crc32.NewIEEE()
	_, _ = h.Write([]byte(fourcc))
	_, _ = h.Write(body)
	_ = binary.Write(&buf, binary.BigEndian, h.Sum32())
	return buf.Bytes()
}

func buildSQPKBody(cmd byte, payload []byte) []byte {
	innerSize := 4 + 1 + len(payload)
	body := make([]byte, innerSize)
	binary.BigEndian.PutUint32(body[0:4], uint32(innerSize))
	body[4] = cmd
	copy(body[5:], payload)
	return body
}

func sqpackTargetBytes(mainID, subID uint16, fileID uint32) []byte {
	var b [8]byte
	binary.BigEndian.PutUint16(b[0:2], mainID)
	binary.BigEndian.PutUint16(b[2:4], subID)
	binary.BigEndian.PutUint32(b[4:8], fileID)
	return b[:]
}

func TestApplyCommandsAndHTTPRangeSkip(t *testing.T) {
	var patch bytes.Buffer
	patch.WriteString(Magic)

	// 1. FHDR
	patch.Write(buildChunk("FHDR", make([]byte, 16)))

	// 2. SqpkHeader on 0a0000.win32.dat0 (version header at offset 0)
	hdrPayload := make([]byte, 3+8+HeaderSize)
	hdrPayload[0] = 'D'
	hdrPayload[1] = 'V'
	copy(hdrPayload[3:11], sqpackTargetBytes(0x0a, 0x0000, 0))
	copy(hdrPayload[11:], bytes.Repeat([]byte{0x5A}, HeaderSize))
	patch.Write(buildChunk("SQPK", buildSQPKBody('H', hdrPayload)))

	// 3. SqpkAddData on 0a0000.win32.dat0 at block 16 (offset 2048), 2 blocks data + 1 block zero
	addPayload := make([]byte, 3+8+12+256)
	copy(addPayload[3:11], sqpackTargetBytes(0x0a, 0x0000, 0))
	binary.BigEndian.PutUint32(addPayload[11:15], 16)
	binary.BigEndian.PutUint32(addPayload[15:19], 2)
	binary.BigEndian.PutUint32(addPayload[19:23], 1)
	copy(addPayload[23:], bytes.Repeat([]byte{0x33}, 256))
	patch.Write(buildChunk("SQPK", buildSQPKBody('A', addPayload)))

	// 4. Huge irrelevant SqpkAddData chunk targeting ex1/010000.win32.dat0 (> 1 MiB) -> should be Range-skipped
	bigSize := (1 << 20) + 4096
	bigPayload := make([]byte, 3+8+12+bigSize)
	copy(bigPayload[3:11], sqpackTargetBytes(0x01, 0x0100, 0))
	binary.BigEndian.PutUint32(bigPayload[11:15], 16)
	binary.BigEndian.PutUint32(bigPayload[15:19], uint32(bigSize>>7))
	patch.Write(buildChunk("SQPK", buildSQPKBody('A', bigPayload)))

	// 5. SqpkDeleteData on 0a0000.win32.dat0 at block 17 (offset 2176), 5 blocks -> +0x0C = 4
	delPayload := make([]byte, 3+8+12)
	copy(delPayload[3:11], sqpackTargetBytes(0x0a, 0x0000, 0))
	binary.BigEndian.PutUint32(delPayload[11:15], 17)
	binary.BigEndian.PutUint32(delPayload[15:19], 5)
	patch.Write(buildChunk("SQPK", buildSQPKBody('D', delPayload)))

	// 6. SqpkFileAdd (compressed) on 000000.win32.index
	rawFile := bytes.Repeat([]byte("INDEXDATA"), 32)
	var compressed bytes.Buffer
	fw, _ := flate.NewWriter(&compressed, flate.DefaultCompression)
	_, _ = fw.Write(rawFile)
	_ = fw.Close()
	compBytes := compressed.Bytes()

	relPath := "sqpack/ffxiv/000000.win32.index"
	var fileAdd bytes.Buffer
	fileAdd.Write([]byte{'A', 0, 0})
	_ = binary.Write(&fileAdd, binary.BigEndian, int64(0))
	_ = binary.Write(&fileAdd, binary.BigEndian, int64(len(rawFile)))
	_ = binary.Write(&fileAdd, binary.BigEndian, int32(len(relPath)))
	_ = binary.Write(&fileAdd, binary.BigEndian, uint16(0))
	_ = binary.Write(&fileAdd, binary.BigEndian, uint16(0))
	fileAdd.WriteString(relPath)

	blkHdr := make([]byte, 16)
	binary.LittleEndian.PutUint32(blkHdr[0:4], 16)
	binary.LittleEndian.PutUint32(blkHdr[8:12], uint32(len(compBytes)))
	binary.LittleEndian.PutUint32(blkHdr[12:16], uint32(len(rawFile)))
	fileAdd.Write(blkHdr)
	fileAdd.Write(compBytes)
	for fileAdd.Len()%blockAlign != 0 {
		fileAdd.WriteByte(0)
	}
	patch.Write(buildChunk("SQPK", buildSQPKBody('F', fileAdd.Bytes())))

	// 7. EOF_
	patch.Write(buildChunk("EOF_", nil))

	patchBytes := patch.Bytes()
	rangeReqs := 0
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Accept-Ranges", "bytes")
		rng := r.Header.Get("Range")
		if rng == "" {
			w.WriteHeader(http.StatusOK)
			_, _ = w.Write(patchBytes)
			return
		}
		rangeReqs++
		startStr := strings.TrimSuffix(strings.TrimPrefix(rng, "bytes="), "-")
		start, err := strconv.Atoi(startStr)
		if err != nil || start < 0 || start >= len(patchBytes) {
			w.WriteHeader(http.StatusRequestedRangeNotSatisfiable)
			return
		}
		w.Header().Set("Content-Range", fmt.Sprintf("bytes %d-%d/%d", start, len(patchBytes)-1, len(patchBytes)))
		w.WriteHeader(http.StatusPartialContent)
		_, _ = w.Write(patchBytes[start:])
	}))
	defer srv.Close()

	outDir := t.TempDir()
	stats, err := ApplyHTTP(context.Background(), srv.Client(), srv.URL, outDir, DefaultTargets)
	if err != nil {
		t.Fatalf("ApplyHTTP failed: %v", err)
	}
	if rangeReqs != 1 || stats.SkippedChunks != 1 {
		t.Fatalf("expected 1 range skip, got rangeReqs=%d skipped=%d", rangeReqs, stats.SkippedChunks)
	}

	datPath := filepath.Join(outDir, "sqpack", "ffxiv", "0a0000.win32.dat0")
	dat, err := os.ReadFile(datPath)
	if err != nil {
		t.Fatalf("read dat0: %v", err)
	}
	if !bytes.Equal(dat[:HeaderSize], bytes.Repeat([]byte{0x5A}, HeaderSize)) {
		t.Fatalf("SqpkHeader bytes mismatch")
	}
	// Block 16 (offset 2048..2176) should still have 0x33, block 17 (offset 2176..) should be EmptyBlock with count-1=4 at +0x0C
	if !bytes.Equal(dat[2048:2176], bytes.Repeat([]byte{0x33}, 128)) {
		t.Fatalf("SqpkAddData block 16 mismatch")
	}
	emp := dat[2176 : 2176+24]
	if binary.LittleEndian.Uint32(emp[0:4]) != 128 || binary.LittleEndian.Uint32(emp[12:16]) != 4 || binary.LittleEndian.Uint32(emp[16:20]) != 0 {
		t.Fatalf("EmptyBlock header mismatch: %x", emp)
	}
	if len(dat) != 2176+5*128 {
		t.Fatalf("unexpected dat length: %d", len(dat))
	}

	idxPath := filepath.Join(outDir, "sqpack", "ffxiv", "000000.win32.index")
	idxData, err := os.ReadFile(idxPath)
	if err != nil {
		t.Fatalf("read index: %v", err)
	}
	if !bytes.Equal(idxData, rawFile) {
		t.Fatalf("SqpkFileAdd decompressed content mismatch")
	}
}

func TestNormalizeShiftedEmptyBlocks(t *testing.T) {
	dir := t.TempDir()
	datPath := filepath.Join(dir, "0a0000.win32.dat0")

	dat := make([]byte, 0x800+2*blockAlign)
	// Block 0 at 0x800: shifted EmptyBlock (count-1 = 7 at +0x10)
	binary.LittleEndian.PutUint32(dat[0x800:], blockAlign)
	binary.LittleEndian.PutUint32(dat[0x800+16:], 7)
	// Block 1 at 0x880: official EmptyBlock (count-1 = 5 at +0x0C) -> must stay untouched
	binary.LittleEndian.PutUint32(dat[0x880:], blockAlign)
	binary.LittleEndian.PutUint32(dat[0x880+12:], 5)

	if err := os.WriteFile(datPath, dat, 0o644); err != nil {
		t.Fatal(err)
	}
	fixed, err := NormalizeShiftedEmptyBlocks(datPath, "")
	if err != nil {
		t.Fatal(err)
	}
	if fixed != 1 {
		t.Fatalf("expected 1 normalized block, got %d", fixed)
	}
	got, err := os.ReadFile(datPath)
	if err != nil {
		t.Fatal(err)
	}
	if binary.LittleEndian.Uint32(got[0x800+12:]) != 7 || binary.LittleEndian.Uint32(got[0x800+16:]) != 0 {
		t.Fatalf("block 0 not normalized: %x", got[0x800:0x800+24])
	}
	if binary.LittleEndian.Uint32(got[0x880+12:]) != 5 || binary.LittleEndian.Uint32(got[0x880+16:]) != 0 {
		t.Fatalf("block 1 modified unexpectedly: %x", got[0x880:0x880+24])
	}
}
