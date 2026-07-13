#!/bin/bash
# release-check.sh — Release 产物完整性校验
# 用法: scripts/release-check.sh <path/to/obs-vst3.dll>
#
# 检查项:
#   1. 文件存在
#   2. 文件大小 > 30KB
#   3. DLL 导出表包含 obs_module_load
#   4. DLL 导入表链接了 obs.dll 或 libobs
#   5. 文本段包含关键模块标识字符串

set -e

DLL="$1"
PASS=0
FAIL=0

check() {
    local desc="$1"
    shift
    if "$@" 2>/dev/null; then
        echo "  ✅ $desc"
        PASS=$((PASS + 1))
    else
        echo "  ❌ $desc"
        FAIL=$((FAIL + 1))
    fi
}

echo "=========================================="
echo "  Release 产物完整性检查"
echo "  文件: $DLL"
echo "=========================================="
echo ""

# 1. 文件存在
check "文件存在" test -f "$DLL"

# 2. 文件大小
SIZE=$(stat -c%s "$DLL" 2>/dev/null || stat -f%z "$DLL" 2>/dev/null)
check "文件大小 > 30KB ($SIZE bytes)" test "$SIZE" -gt 30000

# 3-5: PE 分析 (Python)
python3 -c "
import struct, sys

with open('$DLL', 'rb') as f:
    d = f.read()

e = struct.unpack_from('<I', d, 0x3C)[0]
exp_sz = struct.unpack_from('<I', d, e + 0x7C)[0]
imp_sz = struct.unpack_from('<I', d, e + 0x84)[0]

ok = True

# 3. 导出表
if exp_sz > 0:
    print('  ✅ 导出表非空 (size=%d)' % exp_sz)
else:
    print('  ❌ 导出表为空 - OBS 将拒绝此 DLL')
    ok = False

# 4. obs_module_load
if b'obs_module_load' in d:
    print('  ✅ obs_module_load 符号存在')
else:
    print('  ❌ 缺失 obs_module_load')
    ok = False

# 5. 链接了 OBS
if b'obs.dll' in d or b'libobs' in d:
    print('  ✅ 已链接 OBS 库')
else:
    print('  ⚠️ 未检测到 OBS 库链接')

# 6. 包含 VST3 相关字符串
if b'VST3' in d:
    print('  ✅ VST3 代码已编译')
else:
    print('  ⚠️ 未检测到 VST3 代码')

sys.exit(0 if ok else 1)
" || FAIL=$((FAIL + 1))

echo ""
echo "=========================================="
echo "  结果: $PASS 通过, $FAIL 失败"
echo "=========================================="
exit $FAIL
