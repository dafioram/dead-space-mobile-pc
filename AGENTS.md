# Private-input naming

- In source, documentation, commit messages, PRs and issues, refer to privately
  supplied game archives by inventory labels (A–E), not their original filenames
  or full local paths. D may be called Remastered; E may be called Reanimated.
- Generic synthetic CLI examples such as `A.apk` are fine. Preserve useful
  technical findings: package IDs, versions, hashes, ABIs and internal archive
  member paths are not the private source-archive filename.
- Never commit or distribute supplied archives, extracted game assets, saves,
  recordings, screenshots, dumps, private test fixtures or logs.
