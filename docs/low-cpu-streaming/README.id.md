# AmF X1 untuk siaran langsung dengan penggunaan prosesor yang rendah

Panduan ini menjelaskan cara menjalankan AmF X1 bersamaan dengan program streaming seperti TikTok LIVE Studio atau OBS Studio sambil memastikan penggunaan prosesor tetap rendah. Panduan ini berlaku untuk AmF X1 versi 1.24.0.5 di Windows.

## Apa yang membuat penggunaan prosesor tetap rendah
- Saluran yang tidak memiliki input akan dilewati. Saluran yang hanya memancarkan keheningan dan tidak memiliki efek audio yang disisipkan tidak memerlukan waktu pemrosesan.
- Antarmuka ini melambat dengan sendirinya. Antarmuka ini memperbarui tampilan 30 kali per detik saat Anda menggunakannya, 4 kali per detik saat ada program lain yang ditampilkan di depan, dan sekali per detik saat antarmuka ini diminimalkan. Suara tidak pernah melambat.
- Mode ECO mengurangi setengah laju pembaruan layar saat konsol ditampilkan di layar utama.
- Thread audio berjalan pada inti prosesor yang cepat, dan antarmuka tersebut ditandai sebagai tugas latar belakang ketika ada program lain yang sedang aktif di depan.
- Buffer yang lebih besar berarti jumlah blok kerja yang lebih sedikit namun ukurannya lebih besar. Ukuran buffer berkisar antara 32 hingga 8192 sampel.

## Diukur pada mesin uji kami

Konsol ini mengukur kinerjanya sendiri melalui uji mandiri. Pada mesin uji kami (Linux, menampilkan gambar tanpa kartu grafis, dengan satu inti prosesor yang dihitung), program ini secara keseluruhan menggunakan sekitar 7 persen dari satu inti saat dijalankan di latar depan, sekitar 5 persen saat dijalankan di latar belakang program lain, dan sekitar 4 persen saat diminimalkan.

Angka-angka ini berasal dari mesin tersebut. Komputer Anda akan menampilkan angka yang berbeda. Kami belum membandingkannya dengan produk lain. Kami juga belum mengukurnya di Windows saat TikTok LIVE Studio sedang berjalan.

## Persiapan untuk siaran langsung
- Buka menu TOOLS dan pilih LIVE MODE. Saluran mikrofon akan dilengkapi dengan rangkaian pengolahan suara: pengurangan noise, equaliser mikrofon otomatis, level rider, dan peak remover.
- Level master diatur 3 dB di bawah skala penuh, sehingga sinyal streaming tidak mengalami clipping.
- Fungsi pengawasan telah diaktifkan. Jika kartu suara berhenti berfungsi, konsol akan mengaktifkan kembali suaranya secara otomatis.
- Jendela tersebut hanya menampilkan layar LIVE beserta level dan tombol mute. Untuk menutupnya, diperlukan PIN konsol, sehingga tamu tidak dapat mengubah pengaturan campuran.
- Untuk mengaktifkan LIVE MODE secara otomatis, aktifkan opsi AUTO LIVE di menu TOOLS. Saat program streaming dibuka, LIVE MODE akan otomatis aktif.
- Pada program streaming, pilih output suara AmF X1 sebagai sumber audio.

## Cara menggunakan daya pemrosesan seminimal mungkin
- Perkecil jendela konsol selama siaran langsung. Konsol kemudian akan diperbarui sekali setiap detik.
- Gunakan buffer sebesar 512 atau 1024 sampel. Gunakan buffer kecil hanya saat Anda mendengarkan hasil rekaman Anda sendiri melalui konsol.
- Gunakan efek hanya pada saluran yang membutuhkannya.
- Tutup halaman-halaman yang menampilkan tampilan bergerak berukuran besar, seperti alat analisis, sebelum Anda meminimalkan jendela.

## Batas-batas
- Buffer yang besar akan menambah waktu tunda. Pada 8192 sampel dan 48 kHz, satu blok berlangsung sekitar 171 milidetik.
- Tentukan PIN Anda sendiri di halaman ACCESS sebelum Anda mengandalkan fitur penguncian tersebut. PIN bawaan pabrik sama di setiap salinan program, sehingga tidak memberikan perlindungan apa pun.
- Equalizer mikrofon otomatis mengoreksi nada dengan mengukur suara. Ini adalah pemrosesan sinyal, bukan pembelajaran mesin.

---
Translated from English with DeepL. Names on the screen (LIVE MODE, AUTO LIVE, TOOLS, ACCESS) are in English in the program.
