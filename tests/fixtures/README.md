# 跨平台解密测试样本

`qmc/` 保存原生解密实现的一致性测试向量：

- `*_raw.bin` 与 `*_suffix.bin`：加密测试数据及尾部数据。
- `*_target.bin`：预期解密结果，用于逐字节比较。
- `*_key_raw.bin` 与 `*_key.bin`：输入密钥及预期派生结果。
- `kgm-predefined.bin`：上游 KGM 272 字节常量表，用于验证原生实现常量。

来源为 Unlock Music v1.10.0，提交 `502a55289f08be6971450b71830667e90476b6b9`：
<https://github.com/rainlotus97/unlock-music>。
QMC 样本来自上游 `testdata/`；KGM 常量表从 `src/decrypt/kgm.ts` 的 `MaskV2PreDef` 按原始顺序提取。

这些文件仅用于测试，不随 App 分发。MIT 许可，Copyright © 2019–2021 MengYX，
完整声明见仓库根目录的 `THIRD_PARTY_NOTICES.txt`。测试目录不依赖上游源码。
