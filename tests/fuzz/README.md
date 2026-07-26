# Rules compiler fuzz assets

RF0 只冻结 corpus 命名和 seed 规则；RF2/RF3/RF8 创建实际 fuzz target。

- seed 名称：`<target>-<sha256-prefix>.seed`
- regression 名称：`<target>-<issue-or-date>-<sha256-prefix>.case`
- 每个失败记录工具版本、固定 seed、最小输入和对应普通回归测试。
