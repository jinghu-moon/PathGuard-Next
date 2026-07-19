#!/bin/bash

# PathGuard Next - 参考项目批量下载脚本
# 用法: ./download_reference_repos.sh [目标目录]
# 默认下载到当前目录下的 pathguard-reference-repos/
#
# 分类对应架构设计文档章节：
#   A - 同路线 (mount namespace / Zygisk)     -> 对应 6.1, 8 节
#   B - 旧一代 Hook 路线 (反面教材)             -> 对应 1, 23 节 "不迁移" 部分
#   C - 内核级路线 (取舍参考，不采用)           -> 对应 5.6, 19 节
#   D - 底层平台与 Root 基础设施                -> 对应 6.1 AOSP 先例, 7.4 节
#   E - 控制协议 / IPC 参考                     -> 对应 13 节
#   F - 非 Root 架构对照组                      -> 用于确认产品边界，不采用
#
# 许可证提醒（脚本会在下载后打印，不会自动做任何法律判断）：
#   - Dr-TSNG/ZygiskNext   : v1.4 起限制性许可证，可读不可二次分发/冒充继承者
#   - Dr-TSNG/Hide-My-Applist : v3.4 起限制性许可证，禁止修改、再分发、摘取代码
#   其余仓库许可证请下载后自行查看 LICENSE 文件，本脚本不代替法律判断

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

BASE_DIR="${1:-pathguard-reference-repos}"

if ! command -v git &> /dev/null; then
    echo -e "${RED}错误: 未找到 git 命令，请先安装 git${NC}"
    exit 1
fi

mkdir -p "$BASE_DIR"
cd "$BASE_DIR"

echo -e "${GREEN}开始下载参考项目源码到: $(pwd)${NC}"
echo ""

# 格式: "分类|名称|URL|备注"
repos=(
    "A|NeoZygisk|https://github.com/JingMatrix/NeoZygisk|mount namespace 控制现代范本，重点精读"
    "A|ZygiskNext|https://github.com/Dr-TSNG/ZygiskNext|NeoZygisk 上游，限制性许可证，只读"
    "A|Magisk|https://github.com/topjohnwu/Magisk|官方源码，Zygisk 生命周期第一手参考"
    "A|NoHello|https://github.com/MhmRdd/NoHello|轻量模块，Mount Rule System 参考"
    "A|rvmm-zygisk-mount|https://github.com/j-hc/rvmm-zygisk-mount|最小可行 Zygisk 挂载实现"
    "B|riru_storage_redirect|https://github.com/Magisk-Modules-Repo/riru_storage_redirect|旧版 Riru Hook 方案，反面教材"
    "B|HMA-OSS|https://github.com/frknkrc44/HMA-OSS|Hide-My-Applist 开源分支"
    "B|Hide-My-Applist|https://github.com/Dr-TSNG/Hide-My-Applist|原版，限制性许可证，只读"
    "C|susfs4ksu-module|https://github.com/sidex15/susfs4ksu-module|SUSFS 用户态模块"
    "C|susfs4ksu|https://github.com/Star-Seven/susfs4ksu|内核补丁 GitHub 镜像（原始仓库在 GitLab，按内核版本分支）"
    "C|NoMount|https://github.com/maxsteeel/nomount|内核级 VFS 注入，仅了解技术边界"
    "C|APatch|https://github.com/bmax121/APatch|KernelPatch/syscall-table-hook 路线，第三种后端参照"
    "D|KernelSU|https://github.com/tiann/KernelSU|内核级 Root 方案，su 请求校验参考"
    "D|AOSP-MediaProvider|https://github.com/aosp-mirror/platform_packages_providers_mediaprovider|FuseDaemon 源码，对应 7.4 节 provider 边界"
    "E|Shizuku|https://github.com/RikkaApps/Shizuku|控制协议设计参考 (UDS/请求校验)"
    "E|Sui|https://github.com/RikkaApps/Sui|Shizuku API 的 Root 接口实现"
    "F|Shelter|https://github.com/PeterCxy/Shelter|Work Profile 隔离，非 Root 架构对照组"
    "F|Haven|https://github.com/Kenneth-Cho-InfoSec/Haven|Shelter/Island 现代化 fork"
)

declare -A category_names=(
    [A]="同路线：Zygisk / mount namespace 控制"
    [B]="旧一代 Hook 路线（反面教材）"
    [C]="内核级路线（取舍参考，不采用）"
    [D]="底层平台与 Root 基础设施"
    [E]="控制协议 / IPC 参考"
    [F]="非 Root 架构对照组"
)

total=${#repos[@]}
current=0
declare -A category_done

for entry in "${repos[@]}"; do
    IFS='|' read -r cat name url note <<< "$entry"
    current=$((current + 1))

    if [ -z "${category_done[$cat]}" ]; then
        echo -e "${CYAN}=== 分类 $cat: ${category_names[$cat]} ===${NC}"
        category_done[$cat]=1
    fi

    echo -e "${YELLOW}[$current/$total]${NC} 处理: ${GREEN}$name${NC} ($note)"

    if [ -d "$name/.git" ]; then
        echo -e "  目录已存在，执行 ${YELLOW}git pull${NC} 更新..."
        (cd "$name" && git pull --prune) || echo -e "${RED}  更新失败，跳过${NC}"
    else
        echo -e "  执行 ${YELLOW}git clone${NC}..."
        if git clone --depth 1 "$url" "$name" 2>/dev/null; then
            echo -e "  ${GREEN}克隆成功${NC}"
        else
            echo -e "  ${YELLOW}浅克隆失败，尝试完整克隆...${NC}"
            if git clone "$url" "$name"; then
                echo -e "  ${GREEN}克隆成功${NC}"
            else
                echo -e "  ${RED}克隆失败: $url${NC}"
            fi
        fi
    fi
    echo ""
done

echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}所有项目处理完成！下载目录: $(pwd)${NC}"
echo ""
echo -e "${RED}许可证提醒：${NC}"
echo -e "  - ${YELLOW}ZygiskNext${NC}（Dr-TSNG 原版）: v1.4 起限制性许可证，禁止冒充继承者/重新品牌化 fork"
echo -e "  - ${YELLOW}Hide-My-Applist${NC}（原版）: v3.4 起限制性许可证，禁止修改、再分发、摘取代码片段"
echo -e "  以上两个仓库建议只读参考架构思路，不要复制代码或再分发。"
echo -e "  其余仓库许可证请下载后查看各自 LICENSE 文件确认使用边界。"
echo ""
echo -e "${YELLOW}备注：${NC} susfs4ksu 使用的是 GitHub 镜像（Star-Seven/susfs4ksu），"
echo -e "  原始仓库在 GitLab (gitlab.com/simonpunk/susfs4ksu) 按内核版本分多分支维护，"
echo -e "  如果需要最新内容，请手动切到对应内核版本分支查看，不要只看镜像默认分支。"
