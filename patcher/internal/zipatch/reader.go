package zipatch

import (
	"bufio"
	"context"
	"encoding/binary"
	"fmt"
	"hash/crc32"
	"io"
	"net/http"
	"os"
	"strconv"
	"strings"
	"time"
)

const (
	defaultSkipThreshold = 1 << 20
	maxHTTPRetries       = 5
)

var DefaultTargets = []string{
	"sqpack/ffxiv/000000.win32.dat0",
	"sqpack/ffxiv/000000.win32.index",
	"sqpack/ffxiv/000000.win32.index2",
	"sqpack/ffxiv/0a0000.win32.dat0",
	"sqpack/ffxiv/0a0000.win32.index",
	"sqpack/ffxiv/0a0000.win32.index2",
}

type ApplyStats struct {
	AppliedChunks int
	SkippedChunks int
	SkippedBytes  int64
	DownloadedBytes int64
}

type seekStream interface {
	io.ReadCloser
	SeekTo(offset int64) error
	CanSeekBeyond(remaining int64) bool
}

type fileSeekStream struct {
	f *os.File
	r *bufio.Reader
}

func newFileSeekStream(f *os.File) *fileSeekStream {
	return &fileSeekStream{f: f, r: bufio.NewReaderSize(f, 256*1024)}
}

func (s *fileSeekStream) Read(p []byte) (int, error) { return s.r.Read(p) }
func (s *fileSeekStream) Close() error               { return s.f.Close() }
func (s *fileSeekStream) CanSeekBeyond(rem int64) bool {
	return rem > defaultSkipThreshold
}
func (s *fileSeekStream) SeekTo(offset int64) error {
	if _, err := s.f.Seek(offset, io.SeekStart); err != nil {
		return err
	}
	s.r.Reset(s.f)
	return nil
}

type httpRangeStream struct {
	ctx       context.Context
	client    *http.Client
	url       string
	respBody  io.ReadCloser
	r         *bufio.Reader
	canRange  bool
	onFetched func(int64)
}

func openHTTPRangeStream(ctx context.Context, client *http.Client, url string, offset int64, onFetched func(int64)) (*httpRangeStream, error) {
	s := &httpRangeStream{
		ctx:       ctx,
		client:    client,
		url:       url,
		onFetched: onFetched,
	}
	if err := s.openAt(offset, offset > 0); err != nil {
		return nil, err
	}
	return s, nil
}

func (s *httpRangeStream) openAt(offset int64, requirePartial bool) error {
	var lastErr error
	for attempt := 0; attempt < maxHTTPRetries; attempt++ {
		if attempt > 0 {
			select {
			case <-s.ctx.Done():
				return s.ctx.Err()
			case <-time.After(time.Duration(attempt*attempt) * 500 * time.Millisecond):
			}
		}

		req, err := http.NewRequestWithContext(s.ctx, http.MethodGet, s.url, nil)
		if err != nil {
			return err
		}
		req.Header.Set("User-Agent", "FFXIVChnTextPatch-Souma/1.0")
		if offset > 0 {
			req.Header.Set("Range", fmt.Sprintf("bytes=%d-", offset))
		}

		resp, err := s.client.Do(req)
		if err != nil {
			lastErr = err
			continue
		}
		if offset == 0 && resp.StatusCode == http.StatusOK {
			s.canRange = strings.EqualFold(resp.Header.Get("Accept-Ranges"), "bytes")
			s.respBody = resp.Body
			s.r = bufio.NewReaderSize(resp.Body, 512*1024)
			return nil
		}
		if resp.StatusCode == http.StatusPartialContent {
			if err := verifyContentRangeStart(resp.Header.Get("Content-Range"), offset); err != nil {
				_ = resp.Body.Close()
				return err
			}
			s.canRange = true
			s.respBody = resp.Body
			s.r = bufio.NewReaderSize(resp.Body, 512*1024)
			return nil
		}
		_ = resp.Body.Close()
		if resp.StatusCode >= 500 || resp.StatusCode == http.StatusTooManyRequests {
			lastErr = fmt.Errorf("HTTP %d", resp.StatusCode)
			continue
		}
		if requirePartial && resp.StatusCode == http.StatusOK {
			return fmt.Errorf("服务器忽略了 Range 请求 (返回 200)")
		}
		return fmt.Errorf("请求补丁失败: HTTP %d", resp.StatusCode)
	}
	return fmt.Errorf("请求补丁重试耗尽 (%s): %w", s.url, lastErr)
}

func verifyContentRangeStart(header string, want int64) error {
	if !strings.HasPrefix(header, "bytes ") {
		return fmt.Errorf("非法 Content-Range: %q", header)
	}
	rest := strings.TrimPrefix(header, "bytes ")
	dash := strings.IndexByte(rest, '-')
	if dash <= 0 {
		return fmt.Errorf("非法 Content-Range: %q", header)
	}
	got, err := strconv.ParseInt(rest[:dash], 10, 64)
	if err != nil || got != want {
		return fmt.Errorf("Content-Range 起始偏移不匹配: want=%d got=%q", want, header)
	}
	return nil
}

func (s *httpRangeStream) Read(p []byte) (int, error) {
	n, err := s.r.Read(p)
	if n > 0 && s.onFetched != nil {
		s.onFetched(int64(n))
	}
	return n, err
}

func (s *httpRangeStream) Close() error {
	if s.respBody != nil {
		err := s.respBody.Close()
		s.respBody = nil
		return err
	}
	return nil
}

func (s *httpRangeStream) CanSeekBeyond(rem int64) bool {
	return s.canRange && rem > defaultSkipThreshold
}

func (s *httpRangeStream) SeekTo(offset int64) error {
	_ = s.Close()
	return s.openAt(offset, true)
}

func ApplyFile(ctx context.Context, patchPath, outDir string, targets []string) (ApplyStats, error) {
	f, err := os.Open(patchPath)
	if err != nil {
		return ApplyStats{}, err
	}
	stream := newFileSeekStream(f)
	defer stream.Close()
	return applyStream(ctx, stream, outDir, targets)
}

func ApplyHTTP(ctx context.Context, client *http.Client, url, outDir string, targets []string) (ApplyStats, error) {
	if client == nil {
		client = &http.Client{Timeout: 0}
	}
	var stats ApplyStats
	stream, err := openHTTPRangeStream(ctx, client, url, 0, func(n int64) {
		stats.DownloadedBytes += n
	})
	if err != nil {
		return stats, err
	}
	defer stream.Close()
	res, err := applyStream(ctx, stream, outDir, targets)
	res.DownloadedBytes = stats.DownloadedBytes
	return res, err
}

func applyStream(ctx context.Context, stream seekStream, outDir string, targets []string) (ApplyStats, error) {
	var stats ApplyStats

	magic := make([]byte, len(Magic))
	if _, err := io.ReadFull(stream, magic); err != nil {
		return stats, fmt.Errorf("读取 ZiPatch 魔数失败: %w", err)
	}
	if string(magic) != Magic {
		return stats, fmt.Errorf("无效的 ZiPatch 魔数: %q", magic)
	}

	applier := newPatchApplier(outDir, targets)
	defer applier.closeAll()

	pos := int64(len(Magic))
	var hdr [8]byte

	for {
		if err := ctx.Err(); err != nil {
			return stats, err
		}
		if _, err := io.ReadFull(stream, hdr[:]); err != nil {
			return stats, fmt.Errorf("读取 ZiPatch 块头失败 (off=%d): %w", pos, err)
		}
		size := int(binary.BigEndian.Uint32(hdr[0:4]))
		fourcc := string(hdr[4:8])
		bodyStart := pos + 8
		nextChunkPos := bodyStart + int64(size) + 4

		peekLen := 0
		var peekBuf []byte
		if fourcc == "SQPK" && size > defaultSkipThreshold && len(applier.targets) > 0 {
			peekLen = min(size, 288)
			peekBuf = make([]byte, peekLen)
			if _, err := io.ReadFull(stream, peekBuf); err != nil {
				return stats, fmt.Errorf("预读 SQPK 头部失败: %w", err)
			}
			if target, canSkip := inspectSqpkTarget(peekBuf, applier.platform); canSkip && !applier.wants(target) {
				rem := nextChunkPos - (bodyStart + int64(peekLen))
				if stream.CanSeekBeyond(rem) {
					if err := stream.SeekTo(nextChunkPos); err != nil {
						return stats, fmt.Errorf("跳过无关块 (%s, %d bytes) 失败: %w", target, rem, err)
					}
				} else {
					if _, err := io.CopyN(io.Discard, stream, rem); err != nil {
						return stats, fmt.Errorf("丢弃无关块 (%s) 失败: %w", target, err)
					}
				}
				stats.SkippedChunks++
				stats.SkippedBytes += int64(size) - int64(peekLen)
				pos = nextChunkPos
				continue
			}
		}

		body := make([]byte, size)
		if peekLen > 0 {
			copy(body, peekBuf)
		}
		if _, err := io.ReadFull(stream, body[peekLen:]); err != nil {
			return stats, fmt.Errorf("读取 %s 块载荷失败 (size=%d): %w", fourcc, size, err)
		}
		var crcBuf [4]byte
		if _, err := io.ReadFull(stream, crcBuf[:]); err != nil {
			return stats, fmt.Errorf("读取 %s 校验和失败: %w", fourcc, err)
		}
		expectedCRC := binary.BigEndian.Uint32(crcBuf[:])
		h := crc32.NewIEEE()
		_, _ = h.Write(hdr[4:8])
		_, _ = h.Write(body)
		if actualCRC := h.Sum32(); actualCRC != expectedCRC {
			return stats, fmt.Errorf("%s 块 CRC 校验失败: file=%08X calc=%08X", fourcc, expectedCRC, actualCRC)
		}

		done, err := applier.applyChunk(fourcc, body)
		if err != nil {
			return stats, fmt.Errorf("应用 %s 块失败: %w", fourcc, err)
		}
		stats.AppliedChunks++
		pos = nextChunkPos
		if done {
			break
		}
	}

	if err := applier.closeAll(); err != nil {
		return stats, err
	}
	return stats, nil
}
