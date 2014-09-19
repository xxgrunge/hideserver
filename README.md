1 . masuk ke dir Unreal3.2 kamu
    contoh : cd Unreal3.2
2 . setelah masuk ke dir Unreal3.2, masuk ke dir " src/modules/
3 . jika sudah masuk ke dir " src/modules " kamu wget file hideserver.tar.gz
    contoh : wget http://www.ircdshells.com.ar/servicios/hideserver.tar.gz
4 . setelah selesai wget file hideserver nya, kamu extrak file hideserver nya
    contoh : tar -zxvf hideserver.tar.gz
5 . setelah extract selesai, kamu kembali ke dir Unreal3.2
    cd ../../;ls -la
6 . jika sudah berada di dir " Unreal3.2 " kamu jalan kan file hideserver nya
    contoh : make custommodule MODULEFILE=hideserver

======================== selesai ==============================

Note : Untuk file " unrealircd.conf " kamu harus menambahkan di bawah bagian " loadmodule " dan masukan code berikut :

loadmodule "src/modules/hideserver.so";
hideserver
        {
        disable-map yes;
        disable-links yes;
        map-deny-message "***** Sorry /Map Disable by Administrator.";
        links-deny-message "***** Sorry /Links Disable by Administrator.";
};

====================================

Jika Kamu bingung, silahkan ambil aja file unrealircd.conf di sini "  http://utm.ae/.../.server/unrealircd.conf  "

==============  ./eOF ==============
