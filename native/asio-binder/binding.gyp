{
  "targets": [
    {
      "target_name": "asio_binder",
      "sources": [ "src/main.cc", "src/asiosdk.cc" ],
      "include_dirs": [
        "<!@(node -p \"require('node-addon-api').include\")",
        "../../drivers/asio/sdk"
      ],
      "dependencies": [ "<!(node -p \"require('node-addon-api').gyp\")" ],
      "defines": [ "NAPI_DISABLE_CPP_EXCEPTIONS" ],
      "cflags_cc": [ "-std=c++17", "-Wall", "-Wextra" ],
      "conditions": [
        ["OS=='win'", { "defines": [ "_WIN32_WINNT=0x0600" ], "libraries": [ "-ladvapi32" ] }]
      ]
    }
  ]
}
