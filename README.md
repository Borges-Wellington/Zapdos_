# Zapdos - Rotom initial C++ client (control + proto welcome)

Estrutura:
- `proto/rotom.proto` - seu arquivo .proto (use o repo UnownHash/RotomProtos como referência).
- `proto_gen/` - saída do `protoc --cpp_out=proto_gen proto/rotom.proto`
- `src/` - código fonte (main.cpp, ProtobufInspector.cpp)
- `samples/sample_request.bin` - amostra binária de MitmRequest

Fluxo básico implementado:
1. Conexão websocket para `ws://<rotom>/control` — envia JSON de introdução `{"deviceId":"xx","version":1,...}` e recebe/atende comandos (runJob, reboot, etc). (documentado em RotomProtos README). :contentReference[oaicite:1]{index=1}
2. Conexão websocket (proto) para `ws://<rotom>/` — envia `WelcomeMessage` protobuf serializado; espera `MitmRequest` (LoginRequest) etc. :contentReference[oaicite:2]{index=2}

Build:
1. Gere os sources protobuf:
   ```bash
   protoc --cpp_out=proto_gen --proto_path=proto proto/rotom.proto
