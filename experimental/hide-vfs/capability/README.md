# PathGuard VFS capability probe

这是面向固定 myron / android16-6.12 目标的只读可行性闸门。模块仅检查
prepared DDK 能否解析后续 prototype 所需的导出符号，并通过
/dev/pathguard_vfs_cap_probe 提供状态位图。

模块不会注册 kprobe，不调用任何 VFS helper，不修改 inode 或 file
operation 表，不安装策略，也不隐藏路径。READY 只表示模块加载时链接器
解析到了所需导出符号，不代表 VFS 语义正确，也不会改变 Hide 1.0 准入。

构建必须使用与最小 loader probe 相同的 android16-6.12 prepared DDK，
设备实验前还必须通过 host ELF/KMI 检查。设备实验限制为一次加载、读取
状态、卸载事务，并记录精确模块哈希。
