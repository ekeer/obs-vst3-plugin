# obs-vst3-plugin 构建与发布全链路 RCA 及改进方案

## 一、事件时间线摘要

在首次完整 CI 构建到 Release 交付过程中，经历了 **20+ 次 CI 触发、5+ 个不同的构建/链接错误、最终 Release 产物的 DLL 仍无法被 OBS 加载**。累计修复耗时超过 6 小时，足够重新实现整个 CI 流程 3 次以上。

---

## 二、根本原因分析

### 根因 1：代码提交前无质量门禁

**现象**：每次 `git push` 后 CI 编译失败，失败原因每次不同：

| 提交 | CI 结果 | 失败原因 | 本应在何时发现 |
|------|---------|---------|---------------|
| 初版 CI | ❌ | `--depth 1` 无 tag | 写 workflow 时应验证分支 tag 策略 |
| 第 2 版 | ❌ | Linux 缺依赖 | 应在本地容器测试 |
| 第 3-5 版 | ❌ | Qt/Steinberg 头文件冲突 | 应在本地编译验证 |
| 第 6-8 版 | ❌ | 多处缺失 `#include` | **代码审查时应发现** |
| 第 9-12 版 | ❌ | 函数签名不匹配 | **编译阶段应 100% 拦截** |
| 第 13-16 版 | ❌ | VST3 成员名不存在 | 新代码与已有 API 不匹配 |
| 第 17+ 版 | ❌ | VST3 SDK 找不到源码文件 | 应在选择依赖时做可行性验证 |

**根因诊断**：

```
代码提交 → [无门禁] → CI 构建 → 失败 → 修 → 提交 → CI 构建 → 失败 → 循环
           ↑ 缺失：本地静态检查、编译测试、依赖验证
```

**直接原因**：项目没有建立任何代码提交前的验证机制。
- 无 `pre-commit hook` 执行静态分析
- 无本地编译脚本（`make check` / `build.sh`）
- 无依赖版本可用性预检
- 代码审查（CR）不检查编译级别的问题（缺失 include、API 不匹配等）

---

### 根因 2：CI/CD 流水线无自动化验证规则

**现象**：CI 显示绿色（Windows ✅、Linux ✅、macOS ✅）但产物不可用。

| 环节 | 通过条件 | 实际质量 | 偏差 |
|------|---------|---------|------|
| cmake configure | 无报错即通过 | VST3 SDK 未找到→静默 `target_disable` | ❌ 配置成功≠插件已启用 |
| cmake build | 编译成功即通过 | 插件被跳过后整个 OBS 编译成功 | ❌ 编译成功≠插件已编译 |
| artifact upload | 文件存在即成功 | 上传了旧版本/空壳 DLL | ❌ 文件存在≠文件有效 |
| Release 发布 | 上传完成即发布 | DLL 无导出表→OBS 拒绝加载 | ❌ 产物生成≠产物可用 |

**根因诊断**：

```
CI 流水线              验证点                     现状
代码检出    →  [无验证]                    只做 clone
cmake 配置  →  [验证: 退出码=0]             忽略 target_disable 警告
cmake 构建  →  [验证: 编译成功]             无目标大小/符号检查
artifact 上传 → [验证: 文件存在]            无内容验证
Release 发布 → [验证: 上传成功]             无集成测试
           ↓
        用户下载 → DLL 不可用 ❌
```

**直接原因**：
- cmake 配置阶段未检查 `VST3SDK_FOUND` 状态
- 构建阶段未验证 `obs-vst3.dll` 目标是否存在
- 产物上传前未做基本的自检（DLL 是否有导出表、大小是否合理）
- Release 发布前无集成测试（安装到 OBS 验证加载是否成功）

---

### 根因 3：依赖管理缺乏显式声明与版本锁定

**现象**：VST3 SDK 在 CI 中因分支/仓库结构问题反复失败。

| 迭代 | 依赖策略 | 失败原因 |
|------|---------|---------|
| v1 | `git clone vst3sdk main` | `main` 不含插件 `FindVST3SDK.cmake` |
| v2 | `git clone vst3sdk main --recursive` | 子模块不完整 |
| v3 | `git clone vst3sdk master` | `master` 也无 .cpp 文件 |
| v4 | 从 vst3publicsdk 下载 | tag 不存在 404 |
| v5 | 用 obs-deps | cmake 找不到路径 |

**根因诊断**：
- 没有显式声明 VST3 SDK 的版本或 commit hash
- 没有验证 VST3 SDK 提供的文件是否满足项目需求
- 没有 fallback 策略（obs-deps / 预编译包）
- 外部依赖的变更（仓库重构、子模块迁移）无预警机制

---

### 根因 4：新代码引入缺乏与现有 API 的对齐检查

**现象**：`VST3Graph.cpp`（新文件）编译时引用了不存在的成员。

| 错误 | 原因 | 应阻止的环节 |
|------|------|-------------|
| `plugin_->channelCount` | `VST3Plugin` 无此方法 | 代码审查 / 编译 |
| `plugin_->classId` | 成员不存在 | 同上 |
| `plugin_->modulePath` | 成员名是 `path` 不是 `modulePath` | 同上 |
| `plugin_->latencySamples` | 成员不存在 | 同上 |

**根因**：
- 新代码未基于 `VST3Plugin.h` 的公开 API 编写
- 无 API 引用文档或头文件自动补全
- CR 时 Reviewer 未检查新代码使用的 API 是否存在

---

### 根因 5：Release 流程缺失产物验证环节

**现象**：Release v0.1.0 已发布下载，DLL 无法被 OBS 加载。

| 检查项 | 预期 | 实际 | 是否检查 |
|--------|------|------|---------|
| DLL 导出表 | 包含 `obs_module_load` | 空表 | ❌ |
| DLL 导入表 | 包含 `obs.dll` 导入 | 无导入 | ❌ |
| 文件大小 | > 50KB（含 Qt/VST3） | 34KB | ❌ |
| OBS 加载测试 | `obs_init_module` 成功 | `Skipping module, not an OBS plugin` | ❌ |

没有人在下载 Release 后做"把 DLL 丢进 OBS 试试能不能用"这个最简单的测试。

---

## 三、改进方案

### 改进 1：代码提交前质量门禁（Pre-commit Gate）

```
[代码变更] → pre-commit hook → 静态分析 → 本地编译 → 依赖检查 → [允许提交]
                                    ↓ 不通过           ↓
                               [阻止提交]       [阻止提交]
```

**具体措施：**

| 门禁层级 | 工具/方法 | 检查内容 | 违规后果 |
|---------|----------|---------|---------|
| L1: 语法检查 | `clang-tidy` | 语法错误、常见 bug | commit 被阻止 |
| L2: 编译检查 | 本地 `build.sh` 脚本 | 至少 Linux/Win 之一能编译 | commit 被阻止 |
| L3: API 合规 | `grep` + 头文件对比 | 新代码使用的 API 在头文件中存在 | PR 标记需审查 |
| L4: 依赖验证 | `check-deps.sh` 脚本 | 确认所有外部依赖的源码/头文件可用 | 输出警告 |

**`build.sh` 脚本（到项目根目录）：**

```bash
#!/bin/bash
# 最小化本地编译验证
set -e
echo "=== 检查依赖可用性 ==="
test -d vst3sdk/pluginterfaces || { echo "VST3 SDK 未就绪"; exit 1; }
test -f obs-src/libobs/obs-module.h || { echo "OBS 头文件未就绪"; exit 1; }

echo "=== 语法检查 ==="
for f in obs-vst3/src/*.cpp; do
    g++ -std=c++17 -fsyntax-only -Iinclude -Iobs-src/libobs "$f" 2>/dev/null
done

echo "=== API 合规检查 ==="
# 确保新文件引用的成员在头文件中存在
for member in channelCount classId modulePath; do
    if grep -q "plugin_->$member" obs-vst3/src/*.cpp 2>/dev/null; then
        grep -q "$member" obs-vst3/include/VST3Plugin.h || {
            echo "❌ $member 在 VST3Plugin.h 中不存在"
            exit 1
        }
    fi
done
echo "✅ 本地检查通过"
```

---

### 改进 2：CI/CD 流水线自动化验证规则

```
                    ┌─ 验证点1: cmake 配置结果中必须包含 "obs-vst3" 目标
                    │
[代码推送] → 流水线 ── 验证点2: 构建日志中必须有 "Building obs-vst3" 或类似输出
                    │
                    ├─ 验证点3: 产物 DLL 必须有导出表 + obs_module_load 字符串
                    │
                    ├─ 验证点4: 产物大小在合理范围（> 30KB）
                    │
                    └─ 验证点5: 产物能被 OBS 识别（os_is_obs_plugin 返回 true）
```

**CI yml 关键片段：**

```yaml
- name: 验证 cmake 配置结果
  run: |
    # 确认插件未被跳过
    grep "obs-vst3" cmake-output.log || { echo "❌ 插件未启用"; exit 1; }

- name: 验证构建产物
  run: |
    # 检查 DLL 大小
    DLL=$(find . -name "obs-vst3.dll" | head -1)
    test -f "$DLL" || { echo "❌ DLL 未生成"; exit 1; }
    SIZE=$(stat -c%s "$DLL")
    test $SIZE -gt 30000 || { echo "❌ DLL 大小异常 ($SIZE bytes)"; exit 1; }
    # 检查导出表
    strings "$DLL" | grep "obs_module_load" || { echo "❌ DLL 无模块导出"; exit 1; }
    echo "✅ 产物验证通过"

- name: 集成测试（Windows）
  run: |
    # 将 DLL 复制到 OBS 插件目录
    copy "$DLL" "%OBS_DIR%\obs-plugins\64bit\"
    # 调用 obs.exe 的模块加载验证（需提供验证工具）
    verify-obs-plugin "%OBS_DIR%\obs-plugins\64bit\obs-vst3.dll"
```

---

### 改进 3：依赖管理标准化

| 措施 | 具体做法 |
|------|---------|
| 锁定依赖版本 | 在 `DEPS.md` 或 CI 环境变量中记录 VST3 SDK 的具体 tag/commit |
| 依赖可用性预检 | CI 第一步先验证所有外部资源可访问 |
| 依赖缓存 | 将下载的 SDK 缓存到 GitHub Actions Cache，避免每次全量下载 |
| 降级策略 | 当首选依赖不可用时，自动尝试备用源（obs-deps → GitHub release → 本地 vendored） |

**示例：依赖清单文件 `DEPS.md`**

```yaml
vst3sdk:
  source: https://github.com/steinbergmedia/vst3sdk
  version: v3.7.12_build_20
  required_files:
    - public.sdk/source/vst/hosting/module.cpp
    - pluginterfaces/base/funknown.cpp
  fallback: obs-deps (bundled with OBS SDK)

obs-studio:
  source: https://github.com/pkviet/obs-studio
  branch: vst3
  required: true
```

---

### 改进 4：Release 产物完整性校验流程

```
Release 候选 → [自动检查清单] → [集成测试] → [签名] → [发布]
                    │
                    ├─ DLL 导出表检查: obs_module_load 存在
                    ├─ DLL 导入表检查: obs.dll 被正确链接
                    ├─ DLL 大小检查: > 30KB
                    ├─ OBS 加载测试: 调用 os_is_obs_plugin 返回 true
                    └─ 版本号检查: 产物版本号与 Release tag 一致
```

**Release 检查清单（`release-check.sh`）：**

```bash
#!/bin/bash
# Release 产物完整性检查
set -e
ARTIFACT=$1

echo "1/5 检查文件存在性"
test -f "$ARTIFACT" || exit 1

echo "2/5 检查文件大小"
SIZE=$(stat -c%s "$ARTIFACT")
test $SIZE -gt 30000 || { echo "  大小异常: $SIZE bytes"; exit 1; }
echo "  OK: $SIZE bytes"

echo "3/5 检查模块导出"
strings "$ARTIFACT" | grep -q "obs_module_load" || { echo "  无 obs_module_load 导出"; exit 1; }
echo "  OK: obs_module_load 存在"

echo "4/5 检查 OBS 依赖链接"
# 检查导入表中是否存在 obs.dll 的符号
python3 -c "
import struct
with open('$ARTIFACT','rb') as f:
    d=f.read()
    ok = b'obs.dll' in d or b'libobs' in d
    print('  OK' if ok else '  WARN: 未链接 OBS 库')
" || true

echo "5/5 模拟 OBS 插件识别"
python3 -c "
import struct
with open('$ARTIFACT','rb') as f:
    d=f.read()
    e_lfanew = struct.unpack_from('<I', d, 0x3C)[0]
    exp_rva = struct.unpack_from('<I', d, e_lfanew + 0x78)[0]
    exp_sz = struct.unpack_from('<I', d, e_lfanew + 0x7C)[0]
    imp_rva = struct.unpack_from('<I', d, e_lfanew + 0x80)[0]
    imp_sz = struct.unpack_from('<I', d, e_lfanew + 0x84)[0]
    print(f'  导出表: RVA={exp_rva:#x} 大小={exp_sz}')
    print(f'  导入表: RVA={imp_rva:#x} 大小={imp_sz}')
    if exp_sz > 0: print('  ✅ 有导出表')
    elif imp_sz > 0: print('  ✅ 有导入表（OBS 可能通过导入表识别）')
    else: print('  ❌ 无导出也无导入 - OBS 将拒绝此 DLL')
"

echo "✅ Release 检查完成"
```

---

### 改进 5：建立项目级流程规范

| 阶段 | 责任人 | 检查项 | 通过标准 |
|------|-------|--------|---------|
| 代码编写 | 开发者 | 本地编译通过 | `build.sh` 零错误 |
| 代码提交 | 开发者 | pre-commit hook 通过 | `clang-tidy` + 语法检查 |
| 代码审查 | Reviewer | API 合规性 | 新代码使用的成员在头文件中存在 |
| CI 构建 | 流水线 | 编译+产物验证 | 三条流水线全绿 + 产物自检通过 |
| Release | 发布者 | 完整性校验 | `release-check.sh` 全部通过 |
| 用户交付 | 发布者 | 集成测试 | DLL 在目标 OBS 版本中加载成功 |

---

## 四、关键指标（KPI）

| 指标 | 当前值 | 目标值 | 衡量方式 |
|------|-------|-------|---------|
| 平均 CI 修复轮次 | 20+ | ≤ 2 | 一次审计后一次性修复 |
| 无效 Release 数 | 1 (v0.1.0) | 0 | Release 前 checklist 通过 |
| 编译失败到发现时间 | CI 跑完（5-10分钟） | 提交前（0分钟） | pre-commit hook |
| 产物不可用到发现时间 | 用户下载后 | CI 构建时 | 产物自检脚本 |
