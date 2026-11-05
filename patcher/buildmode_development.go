//go:build development

package main

// localResourceOnly 表示这是本地开发构建：resource/ 目录就是唯一数据源，
// 不向云端同步内置词条，避免覆盖正在编辑的 CSV 与字体。
const localResourceOnly = true
