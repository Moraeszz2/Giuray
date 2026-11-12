<!--
# SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later

# SPDX-FileCopyrightText: 2018 yuzu Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later
-->

<p align="center">
  <img src="dist/branding/giuray_icon_with_logo.png" alt="Giuray" width="600">
</p>

# Giuray

Giuray é um emulador experimental e de código aberto de Nintendo Switch e Switch 2, escrito em C++, com um **multiplayer para The Legend of Zelda: Ocarina of Time**: dois jogadores, cada um no seu PC, no mesmo mundo.

Giuray não tem ligação com a Nintendo. Use apenas jogos e arquivos de sistema obtidos legalmente: jogos, chaves e firmware não vêm com o emulador.

## Baixar

Baixe pela página de [releases](https://github.com/Moraeszz2/Giuray/releases), extraia a pasta inteira e abra **giuray.exe**.

Giuray guarda os dados em `%APPDATA%\giuray` no Windows (e nas pastas `giuray` de `$XDG_DATA_HOME`, `$XDG_CONFIG_HOME` e `$XDG_CACHE_HOME` no Linux e no macOS). Na primeira vez ele oferece importar o perfil de outro emulador instalado (chaves, saves e configurações); escolha **Keep Old Data** para copiar sem mexer no original.

## Multiplayer de Ocarina of Time

1. Os dois abrem o jogo e carregam o save.
2. Aperte **Ctrl+Shift+M** (ou **Tools → Multiplayer (Ocarina of Time)...**):
   - quem **hospeda** clica em **Hospedar** e manda o código da sala para o amigo;
   - quem **entra** digita o código e clica em **Entrar**.
3. Os dois vão para o mesmo lugar: o outro jogador aparece como um segundo Link, que anda, corre, rola, pula e usa espada, escudo e itens com os botões dele.
4. Baús, escadas, portas, interruptores, chefes e eventos da história que um abre aparecem abertos para o outro quando ele entra na área.

Para o progresso bater, os dois devem começar do mesmo ponto (por exemplo, um save novo cada um). Inimigos comuns, munição, rupees e itens são de cada jogador.

Se a janela ficar em "conectando" e nunca chegar em "conectado", a internet de um dos dois não deixa a conexão direta passar: aí usem uma rede virtual como o [Tailscale](https://tailscale.com/download) e entrem pela sala de novo.

## Problemas

Ao [abrir uma issue](https://github.com/Moraeszz2/Giuray/issues), informe a versão do Giuray, o jogo, a CPU, a GPU e o driver, os passos para reproduzir e, se puder, o log da sessão (`%APPDATA%\giuray\log\giuray_log.txt`). Não envie jogos, chaves, firmware nem dados pessoais.

## Compilar

Veja o [guia de compilação](docs/Build.md) e as [orientações de código](docs/Coding.md). Parte da documentação herdada e alguns nomes internos ainda usam os nomes dos projetos de origem; o alvo da interface continua sendo `yuzu`, e o executável gerado é `giuray.exe`.

## Créditos e licença

Giuray é um fork baseado no trabalho dos projetos Eden, yuzu e Sudachi e de seus colaboradores. Os avisos de copyright originais e as licenças de terceiros são mantidos, como exige a licença.

Veja [LICENSE.txt](LICENSE.txt) e os arquivos de código para os termos de licença.
