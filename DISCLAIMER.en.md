# Risks and disclaimer

Notice version: 2026-10-08

CodexSync is an independently maintained open-source tool. It is not an official product of OpenAI, Google or any other cloud provider, and does not imply their endorsement or warranty.

## Software provided as is

This is a development preview. To the extent permitted by applicable law, the software is provided as is, without a promise that it is error-free, continuously available, suitable for a particular purpose or absolutely secure. Unless required by applicable law or agreed in writing, developers and contributors are not liable for data loss, disclosure, service interruption or other loss arising from using or being unable to use this software. The specific warranty and liability provisions are governed by sections 15, 16 and 17 of AGPL-3.0-only and applicable law.

## Back up before syncing

Synchronization, restore, path mappings and concurrent operations may cause incomplete data, conflicts or corruption. Keep an independent backup and test restoration with artificial data first. Do not use this tool as your only copy. Close Codex before applying changes to original directories. System-bound credentials may require signing in again after restoration on another device.

## Conversations and keys are sensitive

Selected local directories may contain conversations, account information, passwords, API keys and plugin settings. Only sync data you are authorized to handle. Check selected directories, exclusions and the cloud destination. Never put the master key inside a sync directory. Keep a separate secure backup: losing the key prevents decryption, and the developers cannot recover it for you.

## Google authorization and third-party services

Hidden Google Drive storage uses drive.appdata; visible backup folders use drive.file and access only files created by this app or authorized by the user. The two storage modes use separate authorization, and existing hidden backups are retained. Refresh tokens are saved in an encrypted local state file. Sync data is uploaded to the service you select; the developers do not host your data. Original mode uploads unencrypted contents. Selective encryption protects only selected contents; directory and file names remain visible. Check the storage mode and encryption selection before uploading. Cloud providers can still receive necessary account, request and storage-usage information. Encryption does not eliminate every risk.

Third-party terms, quotas, fees, account restrictions and availability are determined by the provider. The software does not guarantee that services will remain free or compatible, and will not purchase storage or enable paid services for you. Confirm that you are entitled to use the selected account, service and data.

## Rights reserved

This notice describes risks. It adds no restrictions to the rights to use, modify or distribute granted by AGPL-3.0-only, and does not require waiving rights or liabilities that cannot be excluded by law. Its legal effect depends on applicable law; it is not legal advice.

Checking "I have read and understand these risks" only records that you have read this notice; it is not a waiver of statutory rights. You can view it again in application settings. In a terminal use `codex-sync disclaimer --lang en`; HTTP/C API clients can request `{"op":"disclaimer","language":"en"}`.
