package replace

import (
	"encoding/binary"
	"fmt"
	"os"

	"github.com/Souma-Sumire/FFXIVChnTextPatch-Souma/patcher/internal/sqpack"
)

// Undo 记录一次写入前的可撤销状态，用来在失败或中止后把客户端文件还原成
// 「本次写入之前」的样子，而不是回头去依赖官方原版备份。
//
// 依据是本工具的写入模型：数据块只追加到 dat0 末尾，索引只原地改写 4 字节偏移。
// 因此只要记住 dat0 的原始长度和每个被改写偏移的原值，就能精确撤回本次写入，
// 既不需要拷贝 GB 级文件，也不要求存在与当前游戏版本匹配的原版备份。
type Undo struct {
	indexPath string
	datPath   string
	datSize   int64
	offsets   []undoOffset
}

type undoOffset struct {
	pt   int64
	old  int32
	idx2 bool
}

// NewUndo 在开始写入前记录 dat0 的当前长度，返回可撤销句柄。
func NewUndo(indexPath string) (*Undo, error) {
	datPath := datPathFor(indexPath, 0)
	st, err := os.Stat(datPath)
	if err != nil {
		return nil, fmt.Errorf("读取 %s 长度失败: %w", datPath, err)
	}
	return &Undo{indexPath: indexPath, datPath: datPath, datSize: st.Size()}, nil
}

// Record 保存索引条目被覆盖前的原始偏移，必须在写入该偏移之前调用。
// nil 句柄是合法的空操作，方便 replace 调用方按需传入。
func (u *Undo) Record(entry *sqpack.IndexFile) {
	if u == nil || entry == nil {
		return
	}
	u.offsets = append(u.offsets, undoOffset{pt: entry.Pt, old: entry.DataOffset, idx2: entry.IsIndex2})
}

// Touched 表示本次是否记录过索引写入。
func (u *Undo) Touched() bool { return u != nil && len(u.offsets) > 0 }

// Revert 撤销本次写入：先把索引偏移改回写前的值并刷盘，再把 dat0 截断回写入前的长度。
// 顺序不能颠倒——先截断会让索引在截断到回写之间短暂指向已不存在的数据，掉电即坏索引。
// 返回是否真的改动了客户端文件：什么都没写时返回 false。
func (u *Undo) Revert() (bool, error) {
	if u == nil {
		return false, nil
	}
	reverted := false

	if len(u.offsets) > 0 {
		f, err := os.OpenFile(u.indexPath, os.O_RDWR, 0)
		if err != nil {
			return false, fmt.Errorf("撤销写入时打开 %s 失败: %w", u.indexPath, err)
		}
		wrote := false
		for _, o := range u.offsets {
			pos := o.pt + 8
			if o.idx2 {
				pos = o.pt + 4
			}
			var cur [4]byte
			if _, err := f.ReadAt(cur[:], pos); err != nil {
				_ = f.Close()
				return reverted, fmt.Errorf("读取 %s 索引偏移失败: %w", u.indexPath, err)
			}
			var want [4]byte
			binary.LittleEndian.PutUint32(want[:], uint32(o.old))
			if cur == want {
				continue
			}
			if _, err := f.WriteAt(want[:], pos); err != nil {
				_ = f.Close()
				return reverted, fmt.Errorf("回写 %s 索引偏移失败: %w", u.indexPath, err)
			}
			wrote = true
		}
		if wrote {
			reverted = true
			if err := f.Sync(); err != nil {
				_ = f.Close()
				return reverted, fmt.Errorf("刷盘 %s 失败: %w", u.indexPath, err)
			}
		}
		if err := f.Close(); err != nil {
			return reverted, fmt.Errorf("关闭 %s 失败: %w", u.indexPath, err)
		}
	}

	f, err := os.OpenFile(u.datPath, os.O_RDWR, 0)
	if err != nil {
		return reverted, fmt.Errorf("撤销写入时打开 %s 失败: %w", u.datPath, err)
	}
	defer f.Close()

	st, err := f.Stat()
	if err != nil {
		return reverted, fmt.Errorf("读取 %s 长度失败: %w", u.datPath, err)
	}
	switch {
	case st.Size() < u.datSize:
		return reverted, fmt.Errorf("%s 比写入前更短（当前 %d，记录 %d 字节），请手动还原", u.datPath, st.Size(), u.datSize)
	case st.Size() > u.datSize:
		if err := f.Truncate(u.datSize); err != nil {
			return reverted, fmt.Errorf("截断 %s 失败: %w", u.datPath, err)
		}
		if err := f.Sync(); err != nil {
			return reverted, fmt.Errorf("刷盘 %s 失败: %w", u.datPath, err)
		}
		reverted = true
	}
	return reverted, nil
}
