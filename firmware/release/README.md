# 配布するファームウェア

`clogger.hex` は、配布用 zip（GitHub Releases）に入れるファームウェア。リリースの手順:

1. `firmware/clogger.X/version.h` の `FW_VERSION` を上げてビルドし、実機で確認する。
2. `firmware/clogger.X/dist/default/production/clogger.X.production.hex` をここに `clogger.hex` としてコピーしてコミットする。
3. `git tag v1`（v1, v2 … の連番）を付けて push すると、GitHub Actions（`.github/workflows/release.yml`）が
   `CLogger_vX.Y.Z.zip`（CLogger.exe + ファームウェア + avrdude のライセンス）を作って Releases に登録する。

`clogger.hex` は `version.h` と同じ版のビルドでないと、zip の作成が失敗する（取り違え防止）。
アプリだけの hex（0x0A00 以降）なので、PCツールの Firmware タブで USB 書き込みに使える。
