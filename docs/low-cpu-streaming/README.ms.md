# AmF X1 untuk penstriman langsung dengan penggunaan pemproses yang rendah

Panduan ini menunjukkan cara menjalankan AmF X1 bersama program penstriman seperti TikTok LIVE Studio atau OBS Studio sambil ia menggunakan sedikit pemproses. Ia terpakai untuk AmF X1 1.24.0.5 pada Windows.

## Apa yang menjadikan penggunaan pemproses rendah?
- Saluran yang tiada input akan dilangkau. Saluran yang hanya mengandungi keheningan dan tiada kesan audio disisipkan tidak memerlukan masa pemprosesan.
- Antaramuka memperlahankan dirinya sendiri. Ia mengimbas semula 30 kali sesaat semasa anda menggunakannya, 4 kali sesaat apabila program lain berada di hadapan, dan sekali sesaat apabila ia diminiaturkan. Bunyi tidak pernah diperlahankan.
- Mod ECO mengurangkan kadar redraw kepada separuh semasa konsol berada di hadapan.
- Benang audio berjalan pada teras pantas pemproses, dan antara muka ditandakan sebagai kerja latar belakang apabila program lain berada di hadapan.
- Buffer yang lebih besar bermakna blok kerja yang lebih sedikit dan lebih besar. Saiz buffer bermula dari 32 hingga 8192 sampel.

## Diukur pada mesin ujian kami

Konsol mengukur dirinya sendiri dalam ujian kendiri. Pada mesin ujian kami (Linux, melukis tanpa kad grafik, satu teras pemproses diambil kira), keseluruhan program menggunakan kira-kira 7 peratus daripada satu teras apabila dijalankan di latar hadapan, kira-kira 5 peratus apabila berjalan di latar belakang program lain dan kira-kira 4 peratus apabila diminiaturkan.

Angka-angka ini adalah daripada mesin itu. Komputer anda akan memberikan angka yang berbeza. Kami belum membandingkannya dengan produk lain. Kami belum mengukurnya pada Windows semasa TikTok LIVE Studio sedang berjalan.

## Menyiapkan siaran langsung
- Buka menu TOOLS dan pilih LIVE MODE. Saluran mikrofon mendapat rantaian suara: pengurangan hingar, penyeimbang automatik mikrofon, pelaras paras dan penyingkir kemuncak.
- Master dipegang 3 dB di bawah skala penuh supaya aliran tidak terpotong.
- Pemantau dihidupkan. Jika kad bunyi berhenti, konsol akan memulakan semula bunyi itu sendiri.
- Tetingkap ini hanya memaparkan skrin LIVE dengan tahap dan butang senyap. Untuk meninggalkannya memerlukan PIN konsol, jadi tetamu tidak dapat mengubah suai campuran.
- Untuk memulakan LIVE MODE secara bersendirian, hidupkan AUTO LIVE dalam menu TOOLS. Apabila program penstriman dibuka, LIVE MODE akan bermula.
- Dalam program penstriman, pilih keluaran bunyi AmF X1 sebagai sumber audio.

## Bagaimana menggunakan kuasa pemprosesan paling sedikit
- Minimumkan konsol semasa penstriman. Ia kemudiannya dilakar semula sekali setiap saat.
- Gunakan penimbal 512 atau 1024 sampel. Gunakan penimbal kecil hanya apabila anda mendengar diri anda melalui konsol.
- Hanya letakkan kesan pada saluran yang memerlukannya.
- Tutup tetingkap yang mempunyai paparan bergerak besar, seperti penganalisis, sebelum anda meminimumkannya.

## Batas
- Penimbal yang besar menambah kelewatan. Pada 8192 sampel dan 48 kHz, satu blok bertahan kira-kira 171 milisaat.
- Tetapkan PIN anda sendiri pada halaman ACCESS sebelum anda bergantung pada kunci. PIN kilang adalah sama pada setiap salinan program, jadi ia tidak melindungi apa-apa.
- Pengimbang automatik mikrofon membetulkan nada dengan mengukur suara. Ia adalah pemprosesan isyarat, bukan pembelajaran mesin.

---
Translated from English with DeepL. Names on the screen (LIVE MODE, AUTO LIVE, TOOLS, ACCESS) are in English in the program.
