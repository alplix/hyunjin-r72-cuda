# Hyunjin RC5-72 — WSL2 + NVIDIA HPC SDK kurulum notları

**Amaç:** Hyunjin CUDA-Fortran RC5-72 core'unu RTX 5070 Ti (sm_120 / Blackwell) üzerinde
gerçekten derleyip tam testini (self-test: ALL TESTS PASSED) yapmak.

## ✅ Durum: TAMAMLANDI (2026-08-28)
- HPC SDK **26.5** WSL Ubuntu'ya `.deb` + `dpkg -i` ile kuruldu (`nvfortran 26.5-0`).
- Selftest RTX 5070 Ti üzerinde **`ALL TESTS PASSED`** veriyor.

## Neden WSL2?
NVIDIA HPC SDK (nvfortran) artık **Windows için yayınlanmıyor** (2023'ten beri yalnızca
Linux). Windows'ta nvfortran yok; RTX 50 serisi (sm_120) desteği de Linux sürümünde.
Elimizdeki GPU: **NVIDIA GeForce RTX 5070 Ti** (driver 616.56, CUDA UMD 13.4).

## GPU kısıtı: WSL'de CUDA context
`nvidia-smi` çalışır ama ilk CUDA çağrısı WSL'de gerçekleşir; MooWrap dnetc client aktifken
performans etkilenebilir. Test yine de çalışır.

## Kurulum özeti (gerçekleştirilen adımlar)

### 1) Ubuntu kur
```
wsl --install -d Ubuntu        # (reboot gerekebilir)
wsl -d Ubuntu -u root
```

### 2) GPU'yu doğrula
```
nvidia-smi        # RTX 5070 Ti görünmeli, CC 12.0 (sm_120)
```

### 3) Bağımlılıklar
```
apt-get update
apt-get install -y gcc g++ gfortran make patch git wget ca-certificates \
    libnl-3-200 libnl-route-3-200 libnuma1 libatomic1 libncursesw6 libtinfo6 openssh-client
```

### 4) NVIDIA HPC SDK 26.5 (APT repo — Windows için yok, Linux/WSL)
HPC SDK 26.5, CUDA 12.9 / 13.2 toolchain içerir ve **sm_120** (Blackwell) destekler.
APT repo üzerinden `.deb` ile:
```
curl -LO https://developer.download.nvidia.com/hpc-sdk/ubuntu/amd64/nvhpc-26-5_26.5-0_amd64.deb
dpkg -i nvhpc-26-5_26.5-0_amd64.deb   # nvfortran,nvc,nvcc 13.2 birlikte gelir
```
> `nvhpc-26-5-cuda-multi` (fazladan CUDA 12.9 dosyaları) gerekmedi. İndirme ~5 GB,
> ~22-36 MB/s; `/root` altına `curl` ile çekin (WSL `/tmp` uçucudur).

Ortam:
```
export PATH=/opt/nvidia/hpc_sdk/Linux_x86_64/26.5/compilers/bin:$PATH
export LD_LIBRARY_PATH=/opt/nvidia/hpc_sdk/Linux_x86_64/26.5/compilers/lib:\
    /opt/nvidia/hpc_sdk/Linux_x86_64/26.5/cuda/lib64:$LD_LIBRARY_PATH
```
Kontrol: `nvfortran --version`, `nvcc --version` (13.2, sm_120 destekler).

### 5) Selftest'i derle (sm_120 / cc120)
`hyunjin-r72-cuda` WSL'de `/mnt/c/Users/Alp/Documents/Default Project/hyunjin-r72-cuda`.

`.cuf` kaynaklarını ön-işlemek için `-cpp` şart; module dosyaları bu sırayı zorlar:
**device → math → host → selftest**.
```
cd <proje>
./scripts/build-selftest-linux.sh build/selftest
```
veya elle:
```
nvfortran -O3 -cpp -gpu=cc120 -o hyunjin_selftest \
    src/hyunjin_r72_device.cuf \
    src/hyunjin_r72_math.f90 \
    src/hyunjin_r72_host.cuf \
    test/hyunjin_selftest.cuf
```

### 6) Çalıştır (RTX 5070 Ti)
```
./hyunjin_selftest
```
Beklenen çıktı (dnetc test case #1, mangled L0 + IV-mixed plain):
```
CPU encrypt: clo=562D285A chi=2FB7852A    [PASS]
72-bit key increment round-trip           [PASS]
CUDA devices detected: 1
GPU core status=2 iterations_out=65157
GPU found key = 00000085 FEE1D4C0 53030CC9
expected key  = 00000085 FEE1D4C0 53030CC9
partial matches (n_check) = 1             [PASS]
empty range returns RESULT_NOTHING        [PASS]
ALL TESTS PASSED - Hyunjin core is sound.
```

## Önemli bulgular (uygulama notları)
- **dnetc RC5-72 anahtar formatı "mangled"**: `problem.cpp::__SwitchRC572Format` anahtarı
  core'a vermeden çevirir; plain ayrıca IV ile XORlanır. Core, `rc5_72unitwork.L0` (mangled)
  + IV-mixed plain çalıştırır. Selftest bu gerçek modele göre yazıldı (mangled solution L0
  `{85, FEE1D4C0, 53030CC9}` + plain `{2FF17AF3, 3F3CA653}` → cypher `562D285A/2FB7852A`).
- **nvfortran GPU ISHFT(x, negatif) aritmetik (sign-extend)**: RC5 ROTL için doğru logical
  rotate istiyorsa sağ-kaydırma yarısını 64-bit unsigned araca (işaret maskesiyle) yapın
  (`hy_rotl` içinde ele alındı).
- **bswap**: `hy_bswap32` başlangıçta byte-reverse yerine identity üretiyordu; düzeltildi
  (device/host/math üçünde). Increment (dnetc enumeration) bu yüzden yanlıştı.
- Increment round-trip tek başına bswap yönünü YAKALAMAZ (identity de kapanır); asıl
  doğrulama mangled START + 65157 iterasyonun mangled solution'a ulaşmasıyla yapıldı.

## Build scriptleri
- `scripts/build-selftest-linux.sh` → selftest'i cc80,cc90,cc100,cc120 ile derler/çalıştırır.
- `scripts/build-linux-x86_64.sh` → dnetc client'a core enjekte eder (`-gpu=cc80,cc90,cc100,cc120`).
- `scripts/BUILD-Windows.md` artık geçersiz (HPC SDK Windows'ta yok) → Linux/WSL'ye güncellendi.

## İletişim
Kurulumdan sonra yeni ortamda takılırsanız bu notu/repo'yu yeniden açıp devam edin.
