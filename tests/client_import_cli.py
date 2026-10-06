"""Installed CLI contract using synthetic credentials only; no network access."""
import json
import os
from pathlib import Path
import subprocess
import sys
import uuid

exe, base = sys.argv[1:]
run = Path(base).resolve() / uuid.uuid4().hex
run.mkdir(parents=True)
(run / 'source').mkdir()
env = dict(os.environ)
for name in list(env):
    if name.startswith('CXS_'):
        del env[name]

def cli(*args, payload=None):
    return subprocess.run([exe, *args], input=payload, capture_output=True, env=env, timeout=15)

key = run / 'fixture.key'
assert cli('keygen', '--output', str(key)).returncode == 0
env['CXS_KEY_FILE'] = str(key)
config = run / 'config.json'
config.write_text(json.dumps({'format': 1, 'device': uuid.uuid4().hex, 'state': str(run/'state'),
    'roots': [{'id': 'fixture', 'path': str(run/'source')}], 'exclude': [],
    'remote': {'provider': 'google_drive', 'repository': 'cli-fixture'}}), encoding='utf-8')
fixture_secret = 'synthetic-cli-client-secret'
profile = json.dumps({'installed': {'client_id': 'fixture.apps.googleusercontent.com',
    'client_secret': fixture_secret}}).encode()
result = cli('google-client-import', '--config', str(config), payload=profile)
assert result.returncode == 0, 'CLI client import failed'
assert json.loads(result.stdout)['client_imported'] is True
assert fixture_secret.encode() not in result.stdout + result.stderr
encrypted = (run / 'state/google-client.cxs').read_bytes()
assert encrypted.startswith(b'CXS\x01') and fixture_secret.encode() not in encrypted
duplicate = cli('google-client-import', '--config', str(config), payload=profile)
assert duplicate.returncode != 0 and 'overwrite' in json.loads(duplicate.stdout)['error']
assert (run / 'state/google-client.cxs').read_bytes() == encrypted
print('CLI_IMPORT_PASS: stdin import, encrypted profile, no secret output, overwrite refused')
