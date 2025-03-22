Import("env")

env.Replace(
    FUSESFLAGS=[
        "-Uhfuse:w:0xFB:m",
        "-Ulfuse:w:0x3A:m"
    ]
)