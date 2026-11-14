
cmake_minimum_required(VERSION 3.15)


target_sources(${TARGET_EXE}
    PRIVATE
        atcmd.c
        ../ble/app/ble_init.c
        cmd_shell.c
        iperf.c
        main.c
        mqtt_app/mqtt_cmd.c
        ota_demo.c
        ping.c
        wifi_sta.c
    )


