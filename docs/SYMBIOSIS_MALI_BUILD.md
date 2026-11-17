# Build Emulator Nintendo Switch untuk Mali / MediaTek

Workflow yang disediakan di `.github/workflows/build-symbiosis-mali.yml` membangun APK Android dari **Eden + patch Symbiosis**. Ini adalah emulator berbasis native ARM64 dan Vulkan, bukan aplikasi launcher atau UI tiruan.

## Target dan status

- Target yang disebut oleh Symbiosis: Mali-G57 MC2 / MediaTek MT6789 dengan RAM 8 GB.
- Pemindai format Symbiosis memasukkan `.nsp` sebagai format yang dapat diluncurkan melalui loader Eden.
- Build mengompilasi emulator core native dari sumber Eden dan menerapkan patch Symbiosis untuk Android/Mali.
- Sumber Symbiosis dipatok pada commit `bbc102242633a966f86d74892a9589ec121cab78`; Eden dipatok pada tag `v0.2.1` dan commit `58c1e20ee58efa3900ba616207d460886214480b`.

**Batasan penting:** pembuat Symbiosis menyatakan bahwa patch mereka belum diuji pada perangkat Android fisik. Karena itu, APK yang berhasil dikompilasi bukan bukti bahwa semua game berjalan, atau bahwa Helio G100 akan mencapai FPS tertentu. Uji game, kestabilan Vulkan, suhu, dan kompatibilitas harus dilakukan pada perangkat sebenarnya.

## Mengambil APK

1. Buka tab Actions pada repositori ini.
2. Pilih workflow **Build Mali Nintendo Switch Emulator**.
3. Buka run terbaru yang berstatus berhasil.
4. Di bagian Artifacts, unduh `Symbiosis-Mali-ARM64-debug` (atau varian release saat dipilih manual).
5. Ekstrak ZIP artifact untuk mendapatkan APK.

Workflow juga dapat dijalankan manual melalui **Run workflow** dan pilihan debug/release.

## Batas legal dan file pengguna

Repositori ini tidak menyertakan game, firmware Nintendo, ataupun kunci kriptografi. Gunakan hanya file game dan data sistem yang pengguna berhak gunakan. Jangan mengunggah file tersebut ke repositori atau artefak GitHub.

## Sumber dan lisensi

- Symbiosis: https://github.com/sj0404-collab/symbiosis — GPL-3.0-or-later.
- Eden: https://git.eden-emu.dev/eden-emu/eden — lisensi dan atribusi upstream dipertahankan.
- Workflow hanya mengompilasi sumber publik yang dipatok; tidak mengambil file game, firmware, atau kunci pengguna.
