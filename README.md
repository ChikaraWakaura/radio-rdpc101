# 1. 概要

linux kernel 4.14.84 対応のサン電子(SUNTAC)の RDPC-101 カーネルドライバ(radio-rdpc101.ko)です。

# 2. 経緯

先日、年末大掃除前の取捨選択で貴重品と書かれたダンボール箱を押し入れから発見しました。  
なかに サン電子(SUNTAC) RDPC-101 が入っていました。

買ってたんだ(笑)　と思いながら、そういえば深夜放送をタイマー録音して聞いていたような  
記憶もよみがえりました(笑)

Raspberry PI で、もしかしたら再利用できるかもと、じこじこ進めた結果がここです(笑)

# 3. ハード編

Raspberry PI に接続して通電した感じだと良好のようです。
![通電全景](/img/P_20181222_191253.jpg)

小生は Raspberry PI 3 model B で実験しました。  

   $ uname -a  
   Linux raspi-002 4.14.84-v7+ #1169 SMP Thu Nov 29 16:20:43 GMT 2018 armv7l GNU/Linux  

# 4. ソフト準備編

そのままだと以下の ko がロードされますが動作不可とわかりました。  

    $ pwd
    /lib/modules/4.14.84-v7+/kernel/drivers/media/radio/si470x

    $ ls -al
    total 60
    drwxr-xr-x 2 root root  4096 Dec 22 15:43 .
    drwxr-xr-x 5 root root  4096 Dec 22 17:35 ..
    -rw-r--r-- 1 root root 22592 Nov 30 15:43 radio-i2c-si470x.ko
    -rw-r--r-- 1 root root 28292 Nov 30 15:43 radio-usb-si470x.ko

blacklist 化してロード不可にするのも良いですしリネームしてロード不可も良いと思います。  

    $ cat /etc/modprobe.d/blacklist-si470x.conf
    blacklist radio_usb_si470x

or

    $ sudo mv radio-usb-si470x.ko radio-usb-si470x.ko.org

ネットで検索すると以下のようにわかりました。  

2009/05/10 IBM PC/AT 互換機向け linux-2.6.29.2 で初版公開 [Driver Labo.](http://www.drvlabo.jp/wp/archives/72)  
2009/11/30 Ubuntu 9.10 linux-2.6.31 奮闘記 [dafmemo](http://dafmemo.blogspot.com/2009/11/linux-usb-radio-peercast.html)  
2013/08/21 izumogeiger Raspberry PI 奮闘記 [izumogeiger](https://izumogeiger.blogspot.com/search/label/RDPC-101) [izumogeiger](https://gist.github.com/izumogeiger/6268289)  

[Driver Labo.](http://www.drvlabo.jp/wp/archives/3)様の解説だと複製カスタム物のようで drivers/media/radio/radio-si470x.c で可能かもと。  
USB SnoopyPro での USB データログも公開されてました。  

[Driver Labo.](http://www.drvlabo.jp/wp/archives/72)様の初版(0.0.1)を読むと FM/AM モード切り替えのみが新規であとはベースコードの
なかから  
実動作不要(動作不可?)を削除してるように見えました。  

これなら同じ手法で Raspberry PI で ko のみメイクで  

loading out-of-tree module taints kernel.

↑は無視でいいかと。なんと言ってもレガシーすぎるデバイスですし(笑)  

# 5. 動作確認

kernel ソースコード一式を必要とします。  

    $ sudo apt-get update
    $ sudo apt-get -y install ncurses-dev
    $ sudo apt-get -y install bc
    $ sudo wget https://raw.githubusercontent.com/notro/rpi-source/master/rpi-source -O /usr/bin/rpi-source
    $ sudo chmod +x /usr/bin/rpi-source
    $ sudo rpi-source --skip-gcc

メイクと実行  

    $ cd radio-rdpc101
    $ sudo make clean
    $ sudo make

ここで現物を USB 差してる場合は抜きます。  
リファレンス ko が入ってる場合はアンロードします。  

    $ sudo rmmod radio_usb_si470x

現物が USB 差されていない事を確認してから以下を実行します。  

    $ sudo make install

現物を USB 差してたら dmesg または tail -10 /var/log/syslog で ko ロード確認が出来ます。  

    $ dmesg
    [19306.257313] usb 1-1.4: new full-speed USB device number 9 using dwc_otg
    [19306.397305] usb 1-1.4: New USB device found, idVendor=10c4, idProduct=818a
    [19306.397320] usb 1-1.4: New USB device strings: Mfr=1, Product=2, SerialNumber=0
    [19306.397329] usb 1-1.4: Product: FM Radio
    [19306.397338] usb 1-1.4: Manufacturer: SILICON LABORATORIES INC.
    [19306.442871] radio-rdpc101: USB radio driver for SUNTAC RDPC-101, Version 0.0.1
    [19306.444549] radio-rdpc101: DeviceID=0x29CF ChipID=0x83CA
    [19306.444560] radio-rdpc101: device has firmware version 320.
    [19306.447187] radio-rdpc101: software version 1, hardware version 7
    [19306.565069] radio-rdpc101: set freq FM 80.00 MHz
    [19306.593499] usbcore: registered new interface driver radio-rdpc101

動作テストとして test ディレクトリに check-ioctl を用意してます。  

    $ cd test
    $ make clean;make

Video for Linux version 2 インターフェースの対応 ioctl をテストできます。  

    $ ./check-ioctl

以下のような表示が得られるとテスト終了です。  

    check VIDIOC_QUERYCAP
    check VIDIOC_G_TUNER
    check VIDIOC_S_TUNER
    check VIDIOC_G_FREQUENCY
    check VIDIOC_S_FREQUENCY
    check VIDIOC_ENUM_FREQ_BANDS
    done.

受信周波数設定ソフトは以下です。  

    $ ./setfreq

FM 周波数帯設定は以下です。  

    $ ./setfreq 80.0

AM 周波数帯設定は以下です。  

    $ ./setfreq 954

FM/AM 受信放送の録音は以下のようになります。  

    $ arecord -l
    **** List of CAPTURE Hardware Devices ****
    card 1: Radio [FM Radio], device 0: USB Audio [USB Audio]
      Subdevices: 1/1
      Subdevice #0: subdevice #0

上記のようなキャプチャーデバイスが表示されない場合は何かトラブってます。  
頑張ってデバッグして commit して下さい(笑)  

以下の入力で 5 秒間一般的な生 WAVE ファイルが生成されます。  

    $ arecord -D hw:1,0 -f S16_LE -r 44100 -c 2 -d 5 rec.wav

ALSA 経由でのリアルタイム録音再生は非力 H/W では厳しいと思います。  
以下、例です。

    $ arecord -D hw:1,0 -f S16_LE -r 44100 -c 2 -d 5 | aplay

ボリューム設定として radio-rdpc101 1323 行目で v4l2 (Video for Linux version 2) へ  
0 - 15 と応答していますが alsamixer はレベル表示してくれません(残念)

# 6. その他

root ユーザの直下にインストールされている kernel ソースコード一式は必要に応じて削除して下さい。  

    $ su -
    # rm -fr linux

ふと lsusb -t を見ると衝撃の事実が(笑)  

    $ lsusb
    Bus 001 Device 009: ID 10c4:818a Cygnal Integrated Products, Inc. Silicon Labs FM Radio Reference Design
    Bus 001 Device 003: ID 0424:ec00 Standard Microsystems Corp. SMSC9512/9514 Fast Ethernet Adapter
    Bus 001 Device 002: ID 0424:9514 Standard Microsystems Corp. SMC9514 Hub
    Bus 001 Device 001: ID 1d6b:0002 Linux Foundation 2.0 root hub

    $ lsusb -t
    /:  Bus 01.Port 1: Dev 1, Class=root_hub, Driver=dwc_otg/1p, 480M
        |__ Port 1: Dev 2, If 0, Class=Hub, Driver=hub/5p, 480M
            |__ Port 1: Dev 3, If 0, Class=Vendor Specific Class, Driver=smsc95xx, 480M
            |__ Port 4: Dev 9, If 1, Class=Audio, Driver=snd-usb-audio, 12M
            |__ Port 4: Dev 9, If 2, Class=Human Interface Device, Driver=radio-rdpc101, 12M
            |__ Port 4: Dev 9, If 0, Class=Audio, Driver=snd-usb-audio, 12M

RDPC-101 の si470x カスタムチップは Human Interface Device と答えるファームが入ってる物なんですね。  
これは ALSA に嫌われる要因かもですね(爆笑)  

以上です。
