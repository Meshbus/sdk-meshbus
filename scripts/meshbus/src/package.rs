// SPDX-License-Identifier: Apache-2.0
//! Offline v1 Firmware package creation and authentication.
use crate::{delta, host};
use anyhow::{Result, bail, ensure};
use ciborium::Value;
use clap::{Args, Subcommand};
use ed25519_dalek::{
    Signature, Signer, SigningKey, VerifyingKey,
    pkcs8::{DecodePrivateKey, DecodePublicKey, EncodePublicKey},
};
use serde_json::{Value as Json, json};
use sha2::{Digest, Sha256};
use std::{
    collections::BTreeMap,
    fs,
    path::{Path, PathBuf},
};
const FILES: [&str; 5] = [
    "source.signed.bin",
    "target.signed.bin",
    "patch.newp",
    "manifest.cbor",
    "manifest.sig",
];
const MAX_IMAGE: u64 = 16 * 1024 * 1024;
pub const MAX_PATCH: usize = 24576;

#[derive(Debug, Args)]
pub struct PackageArgs {
    #[command(subcommand)]
    command: PackageCommand,
}
#[derive(Debug, Subcommand)]
enum PackageCommand {
    Create(Box<CreateArgs>),
    Inspect {
        package: PathBuf,
        #[arg(long)]
        json: bool,
    },
    Verify {
        package: PathBuf,
        #[arg(long)]
        manifest_public_key: PathBuf,
        #[arg(long)]
        image_public_key: PathBuf,
        #[arg(long)]
        json: bool,
    },
}
#[derive(Debug, Args)]
pub struct CreateArgs {
    pub source: PathBuf,
    pub target: PathBuf,
    #[arg(default_value = "build/_firmware")]
    pub output: PathBuf,
    #[arg(long,value_parser=["repeater","room","sensor"])]
    pub role: String,
    #[arg(long)]
    pub board_id: String,
    #[arg(long, default_value = "nrf54l15")]
    pub soc_id: String,
    #[arg(long, default_value_t = 0)]
    pub hardware_min: u32,
    #[arg(long,default_value_t=u32::MAX)]
    pub hardware_max: u32,
    #[arg(long, default_value_t = 1)]
    pub partition_abi: u32,
    #[arg(long)]
    pub campaign_id: Option<String>,
    #[arg(long)]
    pub image_key_id: u32,
    #[arg(long)]
    pub image_public_key: PathBuf,
    #[arg(long)]
    pub manifest_key_id: u32,
    #[arg(long)]
    pub security_counter: u32,
    #[arg(
        long,
        required_unless_present = "manifest_signature",
        conflicts_with = "manifest_signature"
    )]
    pub manifest_private_key: Option<PathBuf>,
    #[arg(
        long,
        required_unless_present = "manifest_private_key",
        requires = "manifest_public_key"
    )]
    pub manifest_signature: Option<PathBuf>,
    #[arg(long)]
    pub manifest_public_key: Option<PathBuf>,
    #[arg(long, default_value = "unspecified-external-signer")]
    pub signer_provenance: String,
}

#[derive(Debug)]
pub struct Image {
    pub version: [u32; 4],
    pub counter: u32,
    pub hash: Vec<u8>,
    hash_end: usize,
    digest: Vec<u8>,
    key_hash: Vec<u8>,
    signature: Vec<u8>,
}
fn u16at(d: &[u8], p: usize) -> Result<usize> {
    Ok(u16::from_le_bytes(
        d.get(p..p + 2)
            .ok_or_else(|| anyhow::anyhow!("truncated image"))?
            .try_into()?,
    ) as usize)
}
fn u32at(d: &[u8], p: usize) -> Result<u32> {
    Ok(u32::from_le_bytes(
        d.get(p..p + 4)
            .ok_or_else(|| anyhow::anyhow!("truncated image"))?
            .try_into()?,
    ))
}
type TlvMap = BTreeMap<u8, Vec<Vec<u8>>>;

fn area(d: &[u8], p: usize, magic: usize, expected: Option<usize>) -> Result<(usize, TlvMap)> {
    let n = u16at(d, p + 2)?;
    ensure!(
        u16at(d, p)? == magic && n >= 4 && p + n <= d.len(),
        "invalid MCUboot TLV area"
    );
    ensure!(
        expected.is_none_or(|s| s == n),
        "protected TLV size mismatch"
    );
    let mut cursor = p + 4;
    let mut map = BTreeMap::new();
    while cursor < p + n {
        ensure!(cursor + 4 <= p + n, "truncated TLV entry");
        let kind = d[cursor];
        let len = u16at(d, cursor + 2)?;
        cursor += 4;
        ensure!(cursor + len <= p + n, "TLV entry exceeds area");
        map.entry(kind)
            .or_insert_with(Vec::new)
            .push(d[cursor..cursor + len].to_vec());
        cursor += len;
    }
    Ok((p + n, map))
}
fn one(map: &BTreeMap<u8, Vec<Vec<u8>>>, key: u8, n: usize) -> Result<Vec<u8>> {
    let vals = map
        .get(&key)
        .ok_or_else(|| anyhow::anyhow!("missing MCUboot TLV {key:#x}"))?;
    ensure!(
        vals.len() == 1 && vals[0].len() == n,
        "MCUboot TLV {key:#x} has invalid count or width"
    );
    Ok(vals[0].clone())
}
pub fn parse_image(data: &[u8]) -> Result<Image> {
    ensure!(
        data.len() >= 32 && u32at(data, 0)? == 0x96f3b83d,
        "not an MCUboot image"
    );
    let header = u16at(data, 8)?;
    ensure!(header >= 32, "invalid MCUboot header size");
    let protected = u16at(data, 10)?;
    let offset = header
        .checked_add(u32at(data, 12)? as usize)
        .ok_or_else(|| anyhow::anyhow!("image size overflow"))?;
    ensure!(protected > 0, "image lacks protected security counter");
    let (hash_end, prot) = area(data, offset, 0x6908, Some(protected))?;
    let (_, unprot) = area(data, hash_end, 0x6907, None)?;
    ensure!(
        !unprot.contains_key(&0x50),
        "security counter must be protected"
    );
    Ok(Image {
        version: [
            data[20] as u32,
            data[21] as u32,
            u16at(data, 22)? as u32,
            u32at(data, 24)?,
        ],
        counter: u32::from_le_bytes(one(&prot, 0x50, 4)?.try_into().unwrap()),
        hash: Sha256::digest(data).to_vec(),
        hash_end,
        digest: one(&unprot, 0x10, 32)?,
        key_hash: one(&unprot, 1, 32)?,
        signature: one(&unprot, 0x24, 64)?,
    })
}
pub fn public_key(path: &Path) -> Result<VerifyingKey> {
    let bytes = host::read(path, 65536)?;
    if bytes.len() == 32 {
        return Ok(VerifyingKey::from_bytes(bytes.as_slice().try_into()?)?);
    }
    let text = std::str::from_utf8(&bytes)?;
    if let Ok(key) = VerifyingKey::from_public_key_pem(text) {
        return Ok(key);
    }
    Ok(SigningKey::from_pkcs8_pem(text)?.verifying_key())
}
pub fn verify_image(data: &[u8], key: &VerifyingKey) -> Result<Image> {
    let image = parse_image(data)?;
    let digest = Sha256::digest(&data[..image.hash_end]);
    ensure!(
        digest.as_slice() == image.digest,
        "MCUboot image digest mismatch"
    );
    ensure!(
        Sha256::digest(key.to_public_key_der()?.as_bytes()).as_slice() == image.key_hash,
        "MCUboot signing key hash mismatch"
    );
    key.verify_strict(&digest, &Signature::from_slice(&image.signature)?)?;
    Ok(image)
}
fn num(n: u64) -> Value {
    Value::Integer(n.into())
}
fn version(v: [u32; 4]) -> Value {
    Value::Array(v.into_iter().map(|n| num(n.into())).collect())
}
fn integer(v: &Value) -> Result<u64> {
    match v {
        Value::Integer(n) => Ok(u64::try_from(*n)?),
        _ => bail!("manifest integer expected"),
    }
}
fn bytes(v: &Value, n: usize) -> Result<&[u8]> {
    match v {
        Value::Bytes(b) if b.len() == n => Ok(b),
        _ => bail!("invalid manifest byte string"),
    }
}
fn text(v: &Value) -> Result<&str> {
    match v {
        Value::Text(t) => Ok(t),
        _ => bail!("manifest text expected"),
    }
}
fn vers(v: &Value) -> Result<[u32; 4]> {
    let Value::Array(v) = v else {
        bail!("invalid version tuple")
    };
    ensure!(v.len() == 4, "invalid version tuple");
    let result = [
        u32::try_from(integer(&v[0])?)?,
        u32::try_from(integer(&v[1])?)?,
        u32::try_from(integer(&v[2])?)?,
        u32::try_from(integer(&v[3])?)?,
    ];
    ensure!(
        result[0] <= 255 && result[1] <= 255 && result[2] <= 65535,
        "version component overflow"
    );
    Ok(result)
}
fn encode(m: &[Value]) -> Result<Vec<u8>> {
    let map = Value::Map(
        m.iter()
            .enumerate()
            .map(|(i, v)| (num((i + 1) as u64), v.clone()))
            .collect(),
    );
    let mut b = vec![];
    ciborium::into_writer(&map, &mut b)?;
    Ok(b)
}
fn decode(data: &[u8]) -> Result<Vec<Value>> {
    ensure!(data.len() <= 384, "manifest exceeds 384 bytes");
    let mut cursor = std::io::Cursor::new(data);
    let value: Value = ciborium::from_reader(&mut cursor)?;
    let Value::Map(pairs) = value else {
        bail!("manifest must be a map")
    };
    ensure!(
        pairs.len() == 20 && cursor.position() == data.len() as u64,
        "manifest envelope invalid"
    );
    let mut m = vec![];
    for (i, (k, v)) in pairs.into_iter().enumerate() {
        ensure!(
            integer(&k)? == i as u64 + 1,
            "manifest keys must be ordered 1..20"
        );
        m.push(v);
    }
    ensure!(encode(&m)? == data, "manifest is not canonical");
    validate(&m)?;
    Ok(m)
}
fn validate(m: &[Value]) -> Result<()> {
    ensure!(
        integer(&m[0])? == 1 && integer(&m[7])? == 1,
        "unsupported protocol or partition ABI"
    );
    ensure!(
        (1..=3).contains(&integer(&m[2])?) && !text(&m[3])?.is_empty() && text(&m[3])?.len() <= 128,
        "invalid target identity"
    );
    ensure!(
        text(&m[4])? == "nrf54l15"
            && integer(&m[5])? <= integer(&m[6])?
            && integer(&m[6])? <= u32::MAX.into(),
        "invalid SoC or hardware range"
    );
    bytes(&m[1], 16)?;
    for value in &m[10..=12] {
        bytes(value, 32)?;
    }
    ensure!(
        vers(&m[9])? > vers(&m[8])? && m[18] == Value::Bool(false),
        "remote firmware version must increase; downgrade is forbidden"
    );
    ensure!(
        (89..=MAX_PATCH as u64).contains(&integer(&m[13])?)
            && integer(&m[14])? == 448
            && integer(&m[19])? == 1,
        "unsupported patch geometry"
    );
    for value in &m[15..=17] {
        ensure!(
            integer(value)? <= u32::MAX.into(),
            "manifest integer overflow"
        );
    }
    Ok(())
}
fn newp(source: &[u8], target: &[u8], s: &Image, t: &Image) -> Result<Vec<u8>> {
    let raw = delta::create(source, target)?;
    let mut p = b"NEWP".to_vec();
    for v in [s.version, t.version] {
        p.extend([v[0] as u8, v[1] as u8]);
        p.extend((v[2] as u16).to_le_bytes());
    }
    for n in [raw.len(), source.len(), target.len()] {
        p.extend(u32::try_from(n)?.to_le_bytes());
    }
    p.extend(&s.hash);
    p.extend(&t.hash);
    p.extend(raw);
    ensure!(
        p.len() <= MAX_PATCH,
        "NEWP patch is {} bytes; limit is {MAX_PATCH}",
        p.len()
    );
    Ok(p)
}
fn reconstruct(source: &[u8], target: &[u8], p: &[u8], s: &Image, t: &Image) -> Result<()> {
    ensure!(
        p.len() > 88 && p.len() <= MAX_PATCH && &p[..4] == b"NEWP",
        "invalid NEWP envelope"
    );
    ensure!(
        u32at(p, 12)? as usize == p.len() - 88
            && u32at(p, 16)? as usize == source.len()
            && u32at(p, 20)? as usize == target.len(),
        "NEWP size mismatch"
    );
    ensure!(
        p[24..56] == s.hash && p[56..88] == t.hash,
        "NEWP hash mismatch"
    );
    for (offset, v) in [(4, s.version), (8, t.version)] {
        ensure!(
            p[offset] == v[0] as u8
                && p[offset + 1] == v[1] as u8
                && u16at(p, offset + 2)? == v[2] as usize,
            "NEWP version mismatch"
        );
    }
    ensure!(
        delta::apply(source, &p[88..])? == target,
        "offline reconstruction differs from retained target"
    );
    Ok(())
}
fn manifest_json(m: &[Value]) -> Result<Json> {
    let names = [
        "protocol_version",
        "campaign_id",
        "role",
        "board_id",
        "soc_id",
        "hardware_revision_min",
        "hardware_revision_max",
        "partition_abi",
        "source_version",
        "target_version",
        "source_sha256",
        "target_sha256",
        "patch_sha256",
        "patch_size",
        "chunk_size",
        "image_key_id",
        "manifest_key_id",
        "security_counter",
        "downgrade_allowed",
        "patch_format",
    ];
    let mut map = serde_json::Map::new();
    for (name, v) in names.iter().zip(m) {
        let v = match v {
            Value::Bytes(b) => json!(host::hex(b)),
            Value::Text(t) => json!(t),
            Value::Bool(b) => json!(b),
            Value::Array(_) => json!(vers(v)?),
            _ => json!(integer(v)?),
        };
        map.insert((*name).into(), v);
    }
    Ok(Json::Object(map))
}
pub fn create(args: &CreateArgs) -> Result<Json> {
    ensure!(
        !args.output.exists() || fs::read_dir(&args.output)?.next().is_none(),
        "output directory is not empty"
    );
    let key = public_key(&args.image_public_key)?;
    let source = host::read(&args.source, MAX_IMAGE)?;
    let target = host::read(&args.target, MAX_IMAGE)?;
    let s = verify_image(&source, &key)?;
    let t = verify_image(&target, &key)?;
    ensure!(
        t.version > s.version && t.counter >= s.counter && t.counter == args.security_counter,
        "target version/security counter is invalid"
    );
    let patch = newp(&source, &target, &s, &t)?;
    reconstruct(&source, &target, &patch, &s, &t)?;
    let campaign = match &args.campaign_id {
        Some(v) => host::unhex(v)?,
        None => uuid::Uuid::new_v4().as_bytes().to_vec(),
    };
    let role = match args.role.as_str() {
        "repeater" => 1,
        "room" => 2,
        "sensor" => 3,
        _ => bail!("unknown endpoint role"),
    };
    let m = vec![
        num(1),
        Value::Bytes(campaign),
        num(role),
        Value::Text(args.board_id.clone()),
        Value::Text(args.soc_id.clone()),
        num(args.hardware_min.into()),
        num(args.hardware_max.into()),
        num(args.partition_abi.into()),
        version(s.version),
        version(t.version),
        Value::Bytes(s.hash.clone()),
        Value::Bytes(t.hash.clone()),
        Value::Bytes(Sha256::digest(&patch).to_vec()),
        num(patch.len() as u64),
        num(448),
        num(args.image_key_id.into()),
        num(args.manifest_key_id.into()),
        num(args.security_counter.into()),
        Value::Bool(false),
        num(1),
    ];
    validate(&m)?;
    let manifest = encode(&m)?;
    ensure!(manifest.len() <= 384, "manifest exceeds 384 bytes");
    let signature = if let Some(path) = &args.manifest_private_key {
        SigningKey::from_pkcs8_pem(std::str::from_utf8(&host::read(path, 65536)?)?)?
            .sign(&manifest)
            .to_bytes()
            .to_vec()
    } else {
        let p = args
            .manifest_signature
            .as_ref()
            .ok_or_else(|| anyhow::anyhow!("manifest signature required"))?;
        let sig = host::read(p, 64)?;
        public_key(
            args.manifest_public_key
                .as_ref()
                .ok_or_else(|| anyhow::anyhow!("manifest public key required"))?,
        )?
        .verify_strict(&manifest, &Signature::from_slice(&sig)?)?;
        sig
    };
    let transfer = host::hash(&manifest);
    let mut inventory = json!({"schema_version":1,"transfer_id":transfer,"role":args.role,"board_id":args.board_id,"soc_id":args.soc_id,"partition_abi":args.partition_abi,"image_key_id":args.image_key_id,"manifest_key_id":args.manifest_key_id,"signer_provenance":args.signer_provenance,"compression":"crle","chunk_size":448,"files":{}});
    // Finish all cryptographic work before creating the output. Never leave a success index on failure.
    fs::create_dir_all(&args.output)?;
    for (name, data) in FILES
        .iter()
        .zip([source, target, patch.clone(), manifest, signature])
    {
        fs::write(args.output.join(name), &data)?;
        inventory["files"][name] = json!({"size":data.len(),"sha256":host::hash(&data)});
    }
    host::json(&args.output.join("inventory.json"), &inventory)?;
    host::checksums(&args.output)?;
    Ok(json!({"package":args.output,"transfer_id":transfer,"patch_size":patch.len()}))
}
pub fn inspect(dir: &Path) -> Result<Json> {
    let inventory: Json = serde_json::from_slice(&host::read(&dir.join("inventory.json"), 65536)?)?;
    let m = decode(&host::read(&dir.join("manifest.cbor"), 384)?)?;
    Ok(json!({"inventory":inventory,"manifest":manifest_json(&m)?}))
}
pub fn verify(dir: &Path, manifest_key: &Path, image_key: &Path) -> Result<Json> {
    host::verify_checksums(dir)?;
    let inspected = inspect(dir)?;
    let inv = &inspected["inventory"];
    ensure!(
        inv["schema_version"] == 1
            && inv["compression"] == "crle"
            && inv["files"]
                .as_object()
                .is_some_and(|o| o.len() == 5 && FILES.iter().all(|f| o.contains_key(*f))),
        "invalid package inventory"
    );
    let mut data = Vec::new();
    for name in FILES {
        let b = host::read(&dir.join(name), MAX_IMAGE)?;
        ensure!(
            inv["files"][name]["size"] == b.len() && inv["files"][name]["sha256"] == host::hash(&b),
            "inventory mismatch: {name}"
        );
        data.push(b);
    }
    let manifest = &data[3];
    let m = decode(manifest)?;
    public_key(manifest_key)?.verify_strict(manifest, &Signature::from_slice(&data[4])?)?;
    let transfer = host::hash(manifest);
    ensure!(inv["transfer_id"] == transfer, "transfer ID mismatch");
    let role = ["repeater", "room", "sensor"][(integer(&m[2])? - 1) as usize];
    for (name, value) in [
        ("role", json!(role)),
        ("board_id", json!(text(&m[3])?)),
        ("soc_id", json!(text(&m[4])?)),
        ("partition_abi", json!(integer(&m[7])?)),
        ("image_key_id", json!(integer(&m[15])?)),
        ("manifest_key_id", json!(integer(&m[16])?)),
        ("chunk_size", json!(448)),
    ] {
        ensure!(inv[name] == value, "inventory identity mismatch: {name}");
    }
    let key = public_key(image_key)?;
    let s = verify_image(&data[0], &key)?;
    let t = verify_image(&data[1], &key)?;
    ensure!(
        s.hash == bytes(&m[10], 32)?
            && t.hash == bytes(&m[11], 32)?
            && s.version == vers(&m[8])?
            && t.version == vers(&m[9])?,
        "retained image identity mismatch"
    );
    ensure!(
        t.counter as u64 == integer(&m[17])? && t.counter >= s.counter,
        "security counter mismatch or downgrade"
    );
    ensure!(
        Sha256::digest(&data[2]).as_slice() == bytes(&m[12], 32)?
            && data[2].len() as u64 == integer(&m[13])?,
        "patch identity mismatch"
    );
    reconstruct(&data[0], &data[1], &data[2], &s, &t)?;
    Ok(json!({"verified":true,"transfer_id":transfer,"patch_size":data[2].len()}))
}
pub fn run(args: PackageArgs) -> Result<()> {
    let result = match args.command {
        PackageCommand::Create(a) => create(&a)?,
        PackageCommand::Inspect { package, .. } => inspect(&package)?,
        PackageCommand::Verify {
            package,
            manifest_public_key,
            image_public_key,
            ..
        } => verify(&package, &manifest_public_key, &image_public_key)?,
    };
    host::print(&result)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn signed_manifest_geometry_and_canonical_encoding() {
        let fixture: Json =
            serde_json::from_str(include_str!("../tests/fixtures/dfota-v1.json")).unwrap();
        let bytes = host::unhex(fixture["manifest.cbor"].as_str().unwrap()).unwrap();
        let mut manifest = decode(&bytes).unwrap();
        manifest[13] = num(MAX_PATCH as u64);
        validate(&manifest).unwrap();
        manifest[13] = num(MAX_PATCH as u64 + 1);
        assert!(validate(&manifest).is_err());
        manifest[13] = num(88);
        assert!(validate(&manifest).is_err());
        let mut noncanonical = vec![0xb8, 20];
        noncanonical.extend(&bytes[1..]);
        assert!(decode(&noncanonical).is_err());
        let mut trailing = bytes;
        trailing.push(0);
        assert!(decode(&trailing).is_err());
    }

    #[test]
    fn incompressible_delta_cannot_exceed_staging_capacity() {
        let source = vec![0; 40000];
        let mut seed = 0x12345678u32;
        let target = (0..40000)
            .map(|_| {
                seed ^= seed << 13;
                seed ^= seed >> 17;
                seed ^= seed << 5;
                seed as u8
            })
            .collect::<Vec<_>>();
        let identity = |data: &[u8], revision| Image {
            version: [0, 0, revision, 0],
            counter: 1,
            hash: Sha256::digest(data).to_vec(),
            hash_end: 0,
            digest: vec![],
            key_hash: vec![],
            signature: vec![],
        };
        let error = newp(
            &source,
            &target,
            &identity(&source, 1),
            &identity(&target, 2),
        )
        .unwrap_err();
        assert!(error.to_string().contains("limit is 24576"));
    }
}
