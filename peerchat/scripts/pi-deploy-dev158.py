"""Approved dev.158 deployment only. No DB restore, key change or TURN operation."""
import argparse, hashlib, importlib.util, json, os, pathlib, shutil, sqlite3
import subprocess, time, uuid, zipfile

ROOT = pathlib.Path('/opt/peerchat-test')
STAGE = ROOT / 'staging/dev158-approved-20261015'
VALIDATION = ROOT / 'validation/dev158-approved-20261015'
VERSION = '4.0.2-dev.158'
PIN = '62A181282FE940F03F6A35541220A05F9ED2CB85A41F6DE3B0DF111286BA17E3'
OLD_TOOL = ROOT / 'staging/dev125-25bcad52bd0241feac769346bb3febc3/pi-compatible-update-v2.py'
HELPER = ROOT / 'server/pi_runtime_validation.py'
os.umask(0o077)
spec = importlib.util.spec_from_file_location('checked_update', OLD_TOOL)
update = importlib.util.module_from_spec(spec)
spec.loader.exec_module(update)

def save(path, value):
    with pathlib.Path(path).open('x') as output:
        json.dump(value, output, indent=2)

def run(arguments):
    return subprocess.run([str(x) for x in arguments], check=True, capture_output=True, text=True).stdout.strip()

def extract(package, destination, expected=None):
    assert not destination.exists(), 'Never overwrite staged runtime'
    destination.mkdir(mode=0o700)
    with zipfile.ZipFile(package) as archive:
        names=archive.namelist()
        assert len(names)==len(set(names))
        assert all(pathlib.PurePosixPath(n).name==n and '/' not in n and '\\' not in n for n in names)
        if expected is not None: assert set(names)==set(expected)
        archive.extractall(destination)
    if expected is not None:
        for name, digest in expected.items(): assert update.digest(destination/name)==digest
    for path in destination.iterdir(): path.chmod(0o600)

def active(version):
    pid=int((ROOT/'data'/version/'server.pid').read_text())
    args=(pathlib.Path('/proc')/str(pid)/'cmdline').read_bytes().split(b'\0')
    assert args[:3]==[str(ROOT/'server'/version/'PeerChat.Server').encode(), b'--config', str(ROOT/'config'/version/'server-config.json').encode()]
    return pid

def config():
    return json.loads((ROOT/'config'/VERSION/'server-config.json').read_text())

def test_run(executable, arguments, log):
    with log.open('x') as output:
        result=subprocess.run([str(executable)]+[str(x) for x in arguments], stdout=output, stderr=subprocess.STDOUT)
    assert result.returncode==0, 'Isolated validation failed: '+str(log)
    for line in log.read_text().splitlines():
        if line.startswith('PASS'): print(line)

def preflight():
    active('4.0.2-dev.125')
    manifest=json.loads((STAGE/'linux-arm-manifest.json').read_text())
    assert manifest['Version']==VERSION and manifest['Rid']=='linux-arm' and manifest['CacheCandidate']
    hashes=json.loads((STAGE/'stage-hashes.json').read_text())
    for name,digest in hashes.items(): assert update.digest(STAGE/name)==digest
    assert update.digest(STAGE/('PeerChat.Server-'+VERSION+'-linux-arm.zip'))=='cdfd255e3057f27701d50a6f1013acac5122845921e605fb99cb63c657414acc'
    binary=ROOT/'server'/VERSION
    extract(STAGE/('PeerChat.Server-'+VERSION+'-linux-arm.zip'),binary,manifest['Files'])
    (binary/'PeerChat.Server').chmod(0o700)
    assert run([binary/'PeerChat.Server','--version'])==VERSION
    assert 'not found' not in run(['ldd',binary/'PeerChat.Server'])
    for name,exe in [('runtime','PeerChat.CacheRuntimeTests'),('probe','PeerChat.PiCacheStorageProbe')]:
        archive='pi-dev158-cache-runtime-loopback.zip' if name=='runtime' else 'pi-dev158-storage-probe.zip'
        extract(STAGE/archive,VALIDATION/name)
        (VALIDATION/name/exe).chmod(0o700)
    runtime=VALIDATION/'runtime/PeerChat.CacheRuntimeTests'
    test_run(runtime,[VALIDATION/'safety-fixtures'],VALIDATION/'cache-safety.log')
    test_run(runtime,['--network',VALIDATION/'tls-fixtures'],VALIDATION/'cache-network.log')
    old=json.loads((ROOT/'config/4.0.2-dev.125/server-config.json').read_text())
    evidence={}
    for field in ['CertificatePath','CertificatePasswordFile','TurnRestSharedSecretFile','FcmServiceAccountPath']:
        value=old.get(field)
        if value:
            source=update.confined(value);assert source.is_file()
            target=STAGE/'private-preservation'/source.relative_to(ROOT)
            target.parent.mkdir(parents=True,exist_ok=True,mode=0o700)
            shutil.copy2(source,target);target.chmod(0o600)
            assert update.digest(source)==update.digest(target)
            evidence[str(source.relative_to(ROOT))]=update.digest(source)
    save(STAGE/'preflight.json',{'passed':True,'secret_hashes':evidence,'version':VERSION})
    print('PASS ARM32 runtime, owned-filesystem safety, isolated authenticated cache TLS and private preservation')

def deploy():
    assert json.loads((STAGE/'preflight.json').read_text())['passed']
    active('4.0.2-dev.125')
    result=run(['python3',OLD_TOOL,'--old','4.0.2-dev.125','--new',VERSION,'--zip',STAGE/('PeerChat.Server-'+VERSION+'-linux-arm.zip'),'--sha256','cdfd255e3057f27701d50a6f1013acac5122845921e605fb99cb63c657414acc','--manifest',STAGE/'linux-arm-manifest.json','--pin',PIN])
    report=json.loads(result)
    assert report['result']=='deployed'
    shutil.copytree(STAGE/'private-preservation',pathlib.Path(report['rollback'])/'secret-files')
    save(STAGE/'deployment.json',report)
    print(json.dumps(report))

def enable():
    active(VERSION)
    deployment=json.loads((STAGE/'deployment.json').read_text())
    backup=update.confined(deployment['rollback'])
    current=config();assert not current.get('AttachmentCache',{}).get('Enabled',False)
    assert current['DatabasePath']=='/opt/peerchat-test/data/4.0.2-dev.33/physical-client.db'
    cache=ROOT/'cache/group-attachments'
    assert not cache.exists(), 'Existing cache requires inspection, never overwrite'
    parent=cache.parent
    if parent.exists(): assert parent.is_dir() and not parent.is_symlink()
    else: parent.mkdir(mode=0o700)
    cache.mkdir(mode=0o700)
    mount=json.loads(run(['findmnt','-J','-T',str(cache),'-o','TARGET,SOURCE,FSTYPE,UUID']))['filesystems'][0]
    assert mount['target']=='/' and mount['fstype']=='ext4' and mount['uuid']=='bb15c8e6-d999-4838-be67-5ff200bffa46'
    stats=os.statvfs(cache);total=stats.f_blocks*stats.f_frsize;available=stats.f_bavail*stats.f_frsize
    reserve=max(2147483648,(total*20+99)//100)
    assert available>reserve+10737418240+16777216
    instance=uuid.uuid4().hex
    (cache/'.peerchat-attachment-cache-instance').write_text(instance)
    replacement=dict(current)
    replacement['AttachmentCache']=dict(Enabled=True,MaximumBytes=10737418240,RetentionSeconds=172800,PartialIdleSeconds=21600,MinimumFreeBytes=2147483648,MinimumFreePercent=20,Directory=str(cache),ExpectedFilesystem='UUID='+mount['uuid'],ExpectedMountPoint=mount['target'],InstanceId=instance)
    config_path=ROOT/'config'/VERSION/'server-config.json'
    candidate=config_path.with_name('cache-enabled.staged.json')
    save(candidate,replacement)
    probe=VALIDATION/'probe/PeerChat.PiCacheStorageProbe'
    test_run(probe,[ROOT/'server'/VERSION/'PeerChat.Server.dll',candidate],VALIDATION/'production-storage.log')
    # Prove incorrect filesystem binding fails before changing any index bytes.
    before=update.digest(cache/'cache.sqlite')
    mismatch=json.loads(json.dumps(replacement));mismatch['AttachmentCache']['ExpectedFilesystem']='UUID=unavailable-test-volume'
    rejected=VALIDATION/'wrong-mount-config.json';save(rejected,mismatch)
    with (VALIDATION/'wrong-mount.log').open('x') as output:
        outcome=subprocess.run([str(probe),str(ROOT/'server'/VERSION/'PeerChat.Server.dll'),str(rejected)],stdout=output,stderr=subprocess.STDOUT)
    assert outcome.returncode!=0 and update.digest(cache/'cache.sqlite')==before
    disabled=backup/'dev158-cache-disabled-config.json';shutil.copy2(config_path,disabled)
    original_hashes=update.identities(current['DatabasePath'])
    run(['python3',HELPER,'stop','--version',VERSION,'--port','7443'])
    try:
        update.snapshot(pathlib.Path(current['DatabasePath']),backup/'pre-cache-stopped.sqlite')
        os.replace(candidate,config_path);config_path.chmod(0o600)
        run(['python3',HELPER,'start','--version',VERSION,'--port','7443'])
        health=update.health(replacement,PIN)
        assert update.identities(current['DatabasePath'],original_hashes)==original_hashes
        assert {k:v for k,v in config().items() if k!='AttachmentCache'}=={k:v for k,v in current.items() if k!='AttachmentCache'}
        with sqlite3.connect('file:'+str(cache/'cache.sqlite')+'?mode=ro',uri=True) as db:
            assert db.execute('pragma integrity_check').fetchone()[0]=='ok'
            objects=db.execute('select count(*) from objects').fetchone()[0]
        report=dict(version=VERSION,pid=active(VERSION),rollback=str(backup),cache=str(cache),maximum_bytes=10737418240,retention_seconds=172800,reserve_bytes=reserve,filesystem_bytes=total,available_bytes=available,health=health,index_integrity='ok',objects=objects,wrong_filesystem_rejected=True)
        save(STAGE/'cache-enabled.json',report)
        print(json.dumps(report))
    except Exception:
        if (ROOT/'data'/VERSION/'server.pid').exists(): run(['python3',HELPER,'stop','--version',VERSION,'--port','7443'])
        shutil.copy2(disabled,config_path)
        run(['python3',HELPER,'start','--version',VERSION,'--port','7443'])
        update.health(current,PIN)
        print('Cache activation rolled back to disabled; CURRENT databases and all files retained')
        raise

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('phase',choices=['preflight','deploy','enable']);args=parser.parse_args()
    globals()[args.phase]()
