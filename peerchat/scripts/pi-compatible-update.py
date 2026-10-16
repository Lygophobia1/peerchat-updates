"""Operator-authorized compatible update of /opt/peerchat-test only.
No secrets printed, key changes, DB restore, service installation or TURN changes.
Existing validation helper owns checked SIGTERM/start; rollback reuses current DB.
"""
import argparse, hashlib, json, os, pathlib, shutil, sqlite3, ssl, socket
import subprocess, time, zipfile
from contextlib import closing

ROOT = pathlib.Path('/opt/peerchat-test').resolve()
def confined(path):
    path = pathlib.Path(path).resolve()
    if path == ROOT or ROOT not in path.parents:
        raise RuntimeError('Update path outside isolated deployment')
    return path
def digest(path):
    h = hashlib.sha256()
    with pathlib.Path(path).open('rb') as f:
        for block in iter(lambda: f.read(1024*1024), b''): h.update(block)
    return h.hexdigest()
def run(args):
    return subprocess.run(args, check=True, capture_output=True, text=True).stdout.strip()
def snapshot(database, destination):
    with sqlite3.connect('file:'+str(database)+'?mode=ro', uri=True) as src:
        with sqlite3.connect(destination) as dst:
            src.backup(dst)
            assert dst.execute('pragma integrity_check').fetchone()[0] == 'ok'
    os.chmod(destination, 0o600)
def identities(database, reference=None):
    result = {}
    with sqlite3.connect('file:'+str(database)+'?mode=ro', uri=True) as db:
        tables={r[0] for r in db.execute("select name from sqlite_master where type='table'")}
        names=reference.keys() if reference is not None else ('accounts','devices','archive_device_descriptors','archive_device_grants',
                     'remembered_devices','group_rosters','group_roster_devices','group_device_identity_proofs','group_device_proofs')
        if reference is not None: assert set(reference).issubset(tables), 'Original protected table removed'
        for name in names:
            if name in tables:
                current=[r[1] for r in db.execute('pragma table_info('+name+')')]
                columns=reference[name]['columns'] if reference is not None else current
                assert all(c in current for c in columns), 'Original column removed'
                selected=','.join('"'+c.replace('"','""')+'"' for c in columns)
                rows=sorted(repr(r) for r in db.execute('select '+selected+' from '+name))
                result[name]={'columns':columns,'count':len(rows),'sha256':hashlib.sha256('\n'.join(rows).encode()).hexdigest()}
    return result
def tree_hashes(directory):
    return {str(p.relative_to(directory)):digest(p) for p in sorted(directory.rglob('*')) if p.is_file()}

def backup_cache(directory, destination):
    # Called only after the checked server has stopped. Preserve ciphertext,
    # partials, index and ownership marker together; never restore this over
    # newer writes automatically during binary rollback.
    entries=list(directory.rglob('*'))
    assert not any(p.is_symlink() for p in entries), 'Unexpected cache backup symlink'
    required=sum(p.stat().st_size for p in entries if p.is_file())
    stats=os.statvfs(destination.parent)
    reserve=max(2*1024**3,(stats.f_blocks*stats.f_frsize*20+99)//100)
    assert stats.f_bavail*stats.f_frsize-required>=reserve, 'Insufficient safe backup space'
    shutil.copytree(directory,destination)
    assert tree_hashes(directory)==tree_hashes(destination), 'Restorable cache backup hash mismatch'
    with closing(sqlite3.connect('file:'+str(destination/'cache.sqlite')+'?mode=ro',uri=True)) as db:
        assert db.execute('pragma integrity_check').fetchone()[0]=='ok', 'Cache backup integrity'
    return {'bytes':required,'file_count':sum(p.is_file() for p in entries),'hashes_verified':True,'index_integrity':'ok'}

def cache_health(config, check_index=False):
    cache = config.get('AttachmentCache', {})
    if not cache.get('Enabled', False): return {'enabled':False}
    directory = confined(cache['Directory'])
    assert directory.is_dir() and not directory.is_symlink(), 'Cache storage unavailable'
    assert (directory/'.peerchat-attachment-cache-instance').read_text().strip()==cache['InstanceId']
    mount=json.loads(run(['findmnt','-J','-T',str(directory),'-o','TARGET,UUID']))['filesystems'][0]
    assert mount['target']==cache['ExpectedMountPoint']
    assert 'UUID='+mount['uuid']==cache['ExpectedFilesystem']
    stats=os.statvfs(directory);reserve=max(cache['MinimumFreeBytes'],(stats.f_blocks*stats.f_frsize*cache['MinimumFreePercent']+99)//100)
    assert stats.f_bavail*stats.f_frsize>=reserve, 'Cache free-space reserve'
    if check_index:
        with sqlite3.connect('file:'+str(directory/'cache.sqlite')+'?mode=ro',uri=True) as db:
            assert db.execute('pragma integrity_check').fetchone()[0]=='ok', 'Cache integrity'
    return {'enabled':True,'filesystem_binding':'verified','index_integrity':'ok' if check_index else 'not checked while exclusive server owner is active','reserve':'satisfied','configuration':'unchanged'}
def frame(channel, request):
    wire=json.dumps(request).encode();channel.sendall(len(wire).to_bytes(4,'big')+wire)
    def exact(n):
        out=b''
        while len(out)<n:
            part=channel.recv(n-len(out))
            if not part: raise RuntimeError('Health channel closed')
            out+=part
        return out
    n=int.from_bytes(exact(4),'big');assert 0<n<=100000
    return json.loads(exact(n))
def health(config, pin):
    # Pinned TLS, no trust-all fallback: certificate digest checked before ANY
    # application request. Self-hosted certificate has no public CA anchor.
    context=ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    context.check_hostname=False;context.verify_mode=ssl.CERT_NONE
    context.minimum_version=ssl.TLSVersion.TLSv1_2
    def call(request):
        with context.wrap_socket(socket.create_connection(('127.0.0.1',config['Port']),timeout=8),server_hostname='localhost') as tls:
            assert hashlib.sha256(tls.getpeercert(binary_form=True)).hexdigest().upper()==pin
            return frame(tls,dict(version=1,**request))
    # Existing namespaced validation account only; never registers a new device
    # or touches any user authorization/key/history. No credentials in output.
    login=call(dict(type='login',username='pi-validation-status',password='disposable-status-test-password'))
    assert login.get('ok'), 'Existing validation account authentication failed'
    try:
        state=call(dict(type='friend_state',sessionToken=login['sessionToken']))
        assert state.get('ok'), 'Authenticated discovery failed'
    finally:
        call(dict(type='logout',sessionToken=login['sessionToken']))
    return {'tls_pin':'unchanged','existing_test_auth':'passed','discovery':'passed'}

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--old',required=True);parser.add_argument('--new',required=True)
    parser.add_argument('--zip',required=True);parser.add_argument('--sha256',required=True)
    parser.add_argument('--manifest',required=True);parser.add_argument('--pin',required=True)
    args=parser.parse_args();os.umask(0o077)
    for version in (args.old,args.new):
        assert version.startswith('4.0.2-dev.') and version[10:].isdigit()
    oldexe=confined(ROOT/'server'/args.old);newexe=confined(ROOT/'server'/args.new)
    oldconfig=confined(ROOT/'config'/args.old);newconfig=confined(ROOT/'config'/args.new)
    package=confined(args.zip);manifest=confined(args.manifest)
    assert digest(package)==args.sha256.lower(), 'Staged ZIP hash'
    expected=json.loads(manifest.read_text());assert expected['Version']==args.new and expected['Rid']=='linux-arm'
    if not newexe.exists():
        newexe.mkdir(mode=0o700)
        with zipfile.ZipFile(package) as z:
            assert set(z.namelist())==set(expected['Files']), 'Runtime file allowlist'
            for name in z.namelist():
                assert pathlib.PurePosixPath(name).name==name and '/' not in name and '\\' not in name
            z.extractall(newexe)
    assert set(p.name for p in newexe.iterdir())==set(expected['Files']), 'Staged runtime allowlist'
    for name,sha in expected['Files'].items(): assert digest(newexe/name)==sha.lower(), 'Runtime hash'
    os.chmod(newexe/'PeerChat.Server',0o700)
    assert run([str(newexe/'PeerChat.Server'),'--version'])==args.new
    assert 'not found' not in run(['ldd',str(newexe/'PeerChat.Server')])
    config=json.loads((oldconfig/'server-config.json').read_text())
    database=confined(config['DatabasePath']);assert database.exists()
    assert config['Port']==7443
    health(config,args.pin.upper()) # Establish the same health gate before downtime.
    backup=confined(ROOT/'rollback'/('pre-'+args.new+'-'+time.strftime('%Y%m%dT%H%M%SZ',time.gmtime())))
    backup.mkdir(parents=True,mode=0o700)
    shutil.copytree(oldexe,backup/'server');shutil.copytree(oldconfig,backup/'config')
    assert tree_hashes(oldexe)==tree_hashes(backup/'server')
    assert tree_hashes(oldconfig)==tree_hashes(backup/'config')
    snapshot(database,backup/'online.sqlite')
    # Preserve referenced private configuration files without printing their contents.
    for field in ('CertificatePath','CertificatePasswordFile','TurnRestSharedSecretFile','FcmServiceAccountPath'):
        if config.get(field):
            source=confined(config[field]);target=backup/'private-files'/source.relative_to(ROOT)
            target.parent.mkdir(parents=True,exist_ok=True,mode=0o700)
            shutil.copy2(source,target);os.chmod(target,0o600)
            assert digest(source)==digest(target), 'Private-file backup'
    cache_before=cache_health(config)
    certificate=confined(config['CertificatePath']);cert_hash=digest(certificate)
    before=identities(database)
    report={'old':args.old,'new':args.new,'rollback':str(backup),'zip_sha256':args.sha256,
            'database':str(database),'certificate_file_sha256':cert_hash,'identity_grant_snapshot':before}
    (backup/'manifest.json').write_text(json.dumps(report,indent=2))
    # Config is copied byte-for-byte; certificate/password/FCM/TURN references
    # and data path remain unchanged. Only executable/config location changes.
    if newconfig.exists():
        assert tree_hashes(oldconfig)==tree_hashes(newconfig), 'Previously staged config differs'
    else: shutil.copytree(oldconfig,newconfig)
    for directory in (ROOT/'data'/args.new,ROOT/'data'/args.new/'tmp',ROOT/'logs'/args.new):
        confined(directory).mkdir(parents=True,exist_ok=True,mode=0o700)
    assert not (ROOT/'data'/args.new/'server.pid').exists(), 'Candidate already owns a PID'
    helper=str(ROOT/'server/pi_runtime_validation.py')
    run(['python3',helper,'stop','--version',args.old,'--port','7443'])
    try:
        snapshot(database,backup/'stopped.sqlite')
        if cache_before['enabled']:
            report['cache_stopped_integrity']=cache_health(config,True)
            snapshot(confined(config['AttachmentCache']['Directory'])/'cache.sqlite',backup/'cache-stopped.sqlite')
            report['cache_backup']=backup_cache(confined(config['AttachmentCache']['Directory']),backup/'cache-files')
        before=identities(database);report['identity_grant_snapshot']=before
        run(['python3',helper,'start','--version',args.new,'--port','7443'])
        report['health']=health(config,args.pin.upper())
        report['health']['cache']=cache_health(config)
        assert digest(certificate)==cert_hash and digest(oldconfig/'server-config.json')==digest(newconfig/'server-config.json')
        assert identities(database,before)==before, 'Original identity/grant content changed; inspect without restoring DB'
        report['result']='deployed';(backup/'manifest.json').write_text(json.dumps(report,indent=2))
        print(json.dumps({k:report[k] for k in ('old','new','rollback','zip_sha256','health','result')}))
    except Exception:
        # Binary rollback only; NEVER overwrite newer durable user writes.
        newpid=ROOT/'data'/args.new/'server.pid'
        if newpid.exists(): run(['python3',helper,'stop','--version',args.new,'--port','7443'])
        run(['python3',helper,'start','--version',args.old,'--port','7443'])
        health(config,args.pin.upper());report['result']='rolled_back_current_database_preserved'
        (backup/'manifest.json').write_text(json.dumps(report,indent=2))
        raise
if __name__=='__main__': main()
