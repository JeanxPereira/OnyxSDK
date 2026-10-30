# OnyxSDK — Levantamento para refatoração (30/10/2026)

Estado medido em `feat/host-ui-hooks` @ `6dd2be6` (4 commits à frente de
`origin/main`, 8 atrás; o delta remoto é encoding de normal map + fixes de
macOS, 102 linhas em `Source/`+`Include/`, não muda nada abaixo).

Sete varreduras independentes, todas read-only, estão em
`docs/design/2026-10-30-survey/` (api, app, render, core, build, bugs,
consumers). Este documento é a síntese e a proposta. Cada afirmação aponta
para o apêndice que a sustenta.

---

## 0. Veredito em um parágrafo

O **contrato de módulo** (`IGameModule`, `Workspace`, `DecoderRegistry`,
`TypeRegistrar`, `Jobs`, `EventBus`, `DiagSink`, ~500 LOC) é pequeno,
limpo, genérico e é o que os consumidores usam. O CLI compartilha o mesmo
caminho de parse/decode. Isso é o que vale a pena preservar.

**Tudo em volta** é onde está o peso, os bugs e a quebra de genericidade:
uma segunda arquitetura legada ainda ligada e viva no consumidor; uma Shell
que toma o main loop e que dois de três consumidores ejetaram em semanas;
um renderer sem RAII cuja correção depende de um stall síncrono por
chamada; um data model com os campos do God of War como tipo público; e
~117 headers todos públicos, 41% deles uma tabela de ícones.

A promessa de agosto ("um toolkit novo escreve conhecimento de formato,
zero UI, zero render, zero CLI") foi cumprida em formato e CLI e **não**
foi cumprida em UI. A prova de generalidade (segundo módulo com zero edits
no SDK) foi cortada e nunca substituída por algo equivalente; o que
aconteceu no lugar foi o mercado real: Icon-Composer e SF-Symbols saíram.

---

## 1. O design de agosto vs. o que existe

| §design 2026-08-18 prometeu | Estado hoje | Apêndice |
|---|---|---|
| 3 targets (Core sem UI/GPU, Render, Shell), include ilegal = erro de link | Targets existem no CMake. `LayerGuard` só grepa `imgui\|GLFW` em Core/Render sobre arquivos com par .cpp/.h; Shell sem regra. `SFSymbols.h` chega em 4 TUs de Core via `ITypeHandler.h`. Configure baixa GLFW+ImGui+glslang+Vulkan mesmo pra consumidor Core-only. | build §1, api §5 |
| Componentes opt-in (`app.Use(Viewport3D())`), binário sem o que não usa | Não existe `Use()`. `Onyx::Onyx` é agregado INTERFACE de tudo. Só FFmpeg e doctest são gateados. | build §1 |
| Singletons aposentados: `TypeCatalog::Get`, `TypeRegistry::Get`, `AssetVisibility::Get`, `AppConfig::Get` | `TypeCatalog::Get` 102 call sites, `Logger` 170, `AppConfig::Get` (ponteiro nulável), `AssetVisibility`, `ToolkitApi` (aborta se não inicializado), `GetGlobalContext`, `CameraPanel` static. `Workspace` recebe `TypeCatalog&` que na prática é o singleton. | api §2, core §6 |
| Público = `Include/Onyx/**` exceto `Detail/**` | `Detail/` não existe. 117 headers públicos, incluindo `Widgets.h`, `EventManager.h`, `Panels/*`, `VideoPlayer.h` (com libav e ImGui). | api §3 |
| RAII Vulkan (`Unique<T>`, destruição diferida por fence) | Zero RAII. Pares Create/Destroy manuais; `Clear()` com 73 linhas de Destroy; destrutor `= default` não libera nada. Use-after-free real no resize. | render §1, §6 |
| Offscreen-first: headless é o mesmo code path | Duas orquestrações (`Viewport3D::RenderFrame` e `RenderToImage`), convenções de projeção opostas, headless não desenha grid/skeleton, `RenderToImage` recompila pipelines por chamada. | render §7 |
| Viewers pagas uma vez, servem todo jogo | `ViewerKind` é enum fechado {Image, Text, Scene}; `DocumentBrowser.cpp:62-92` hardwira as três; MinimalViewer reimplementa o opener pra adicionar hex. Módulo não contribui viewer sem editar o SDK. | core §2 |
| Nenhum nome de jogo em Core/Render | `MeshData.h:16-21` (`isRigid` "GOWR", `meshHash`), `ObjectData.h` (GOW1/GOW2), `AnimationData.h` porta o bit-stream do Go, `SceneNode.flipZ=true` por causa do GOW2, `App.cpp:425-432` "gow1/gow2/ragnarok", `RecentFiles.gameHint`, `macos_menu.m` `GOWMenuItemHandler`, Q.14 fixed-point em `JointPalette.cpp`/`AnimationPlayer.cpp`. | api §6, core §3, render §8 |
| Thread manifest: main + worker pool, uma fila | `TaskManager` sobe `hardware_concurrency` threads que ninguém usa, coexiste com `JobQueue`; `VideoPlayer` tem 3 threads próprias com races; um `VkQueue` sem mutex. | app §7, bugs #1, #9, #13 |
| `EventBus` RAII como único bus | Existe. `EventManager` (macro, mutex global, UB ao desinscrever no handler) coexiste e é o que `Window.cpp` usa. | api §1, bugs #7 |
| Segundo toolkit com zero edits no SDK | Cortado. O que houve foi o oposto: Icon-Composer (1.270 linhas pra sair) e SF-Symbols ("the window loop is OURS now"). | consumers |

---

## 2. Por que está pesado

Em ordem de impacto medido no código (nenhum profile foi rodado; isto é
leitura estática, ver bugs #13-16, render §2, app §6, build §3):

1. **`Resources::OneShot` é síncrono** (`VkResources.cpp:344-438`): cria
   pool + command buffer + fence, submete e bloqueia em
   `vkWaitForFences(UINT64_MAX)` **por chamada**. Chamado 3× por batch em
   `SceneRendererVk::Build` (+1 por textura), 1× por frame de vídeo
   decodificado (`App/TexturePool.cpp:58`), 1× por redraw de cada viewport
   (`Viewport3D.cpp:414`). Animação tocando = um submit bloqueante por
   frame. Cena com 100 partes e 50 texturas ≈ 350 round-trips seriais na
   UI thread.
   - **Agravante estrutural:** `SceneRendererVk.h:49-66` documenta que
     todo UBO/SSBO/VBO é single-buffered e sem barrier, "seguro só porque
     OneShot bloqueia". Não dá pra pipelinar o frame sem reescrever os 7
     buffers juntos. Isso não é otimização: é redesenho.
2. **Sem `VkPipelineCache`** (`Pipelines.cpp:311/463/605/769`). Cada
   `Viewport3D` e cada `RenderToImage` recompila scene, grid, background e
   overlay.
3. **Hot spots por frame na Shell:** `StatusBar` copia até 4096 entradas de
   log por frame sem clipper (`StatusBar.cpp:123-181`); `DocumentBrowser`
   desenha toda a árvore sem clipper com alocação de string por nó
   (`DocumentBrowser.cpp:100-209`); `NativeWindow::beginFrame` chama
   `SetWindowLong` + `WS_EX_COMPOSITED` todo frame
   (`NativeWindow_windows.cpp:190-205`), flag conhecida por brigar com
   swapchain flip-model.
4. **Peso de compilação:** sem PCH, sem unity. `SFSymbols.h` (6686 linhas,
   41% de todas as linhas de header) entra em 24 TUs via `ITypeHandler.h`
   e `TypeVisuals.h`, logo via `App.h`, `Window.h`, `Onyx.h`. `Onyx.h`
   alcança 81 dos 117 headers. Fan-in máximo ~52 TUs
   (`ObjectData.h`, `BoundingBox.h`, `TypeId.h`).
5. **Configure baixa e compila tudo** (glslang é o maior) mesmo pra quem só
   quer Core. Icon-Composer precisou descobrir `ONYX_COMPONENT_MEDIA` pra
   não baixar FFmpeg; SF-Symbols setou `ONYX_BUILD_MEDIA` (chave errada,
   no-op).
6. **Threads ociosas:** `TaskManager` sobe N threads no construtor da
   Window (`Window.cpp:174`) e nada cria task.
7. **Logger:** ring de 4096 com `vector::erase(begin)` O(n) sob mutex
   global; file sink abre/fecha o arquivo por linha; formata antes de
   filtrar nível (`Logger.cpp:93-98, 240`).
8. **Materiais por parte:** cada parte tem UBO + descriptor set + 7
   samplers, sem dedupe. `GpuVertex` tem 104 bytes/vértice e o parser emite
   o layout exato da GPU. `m_bakeCache` cresce sem limite.

---

## 3. Por que está bugado

Os 12 que importam, do apêndice `survey-bugs.md` (20 ranqueados) e
`survey-render.md`. Todos por leitura; os marcados ⚠ precisam de repro.

| # | Defeito | Gatilho | Onde |
|---|---|---|---|
| 1 | **Use-after-free de GPU no resize** | Redimensionar dock/janela com 2 frames in flight: `target.Destroy()` imediato enquanto o pass de ImGui do frame anterior ainda amostra a view | `Viewport3D.cpp:345-347`, `OffscreenTarget.cpp:88-97`, `Window.cpp:1007` |
| 2 | **Skinning lê fora do SSBO** | Índice de bone do parser > `jointMap.size()`; sem validação, sem robustness feature; `UploadBatchPalettes` trunca em silêncio | `SceneRendererVk.cpp:419-428, 728`, `VkContext.cpp:336-350` |
| 3 | **Hang eterno após falha de submit** | `vkQueueSubmit2` falha → fence fica resetado → próximo `vkWaitForFences(UINT64_MAX)` nunca volta. Nenhum VkResult do frame loop é checado | `Window.cpp:1007-1133` |
| 4 | **VideoPlayer: race no seek** | Arrastar o slider: `avcodec_flush_buffers` na thread de demux enquanto decode usa o mesmo `AVCodecContext`; filas limpas sem lock | `VideoPlayer.cpp:341-376` |
| 5 | **VideoPlayer: leak + double-free** | Toda limpeza de fila descarta `AVPacket*` sem `av_packet_free`; `sws_freeContext` não zera o ponteiro e `Close()` roda duas vezes | `VideoPlayer.h:57`, `VideoPlayer.cpp:268-310, 297` |
| 6 | **Parser ISO com input malformado** | `len` não inicializado, `Read` não checado (rootSize de 4 GB em EOF = loop quase infinito), over-read quando `len < 34`, extent apontando pra si mesmo = stack overflow | `IsoFileSystem.cpp:71-124` |
| 7 | **Config do usuário destruída por um typo** | Erro de parse TOML → defaults → próximo save sobrescreve o arquivo. Save não é atômico, só acontece em `~Window`, e 12 `std::exit(-1)` no construtor pulam o destrutor | `Settings.cpp:47-52`, `AppConfig.cpp:82-86, ~280`, `Window.cpp:196-211` |
| 8 | **`EventManager::post` UB** | Handler que desinscreve durante o post apaga o nó do multimap sendo iterado; handler que lança aborta a entrega pros demais | `EventManager.h:160-174` |
| 9 | **Slices intercaladas corrompem** | `IFile` tem cursor stateful; `SliceFile` re-seeka o parent a cada read; parse (lane por doc) e decode (lane 0) podem sobrepor no mesmo `IFile` | `SliceFile.h:25-37` |
| 10 | **Paths não-ASCII falham no Windows** | `path.string()` = code page ANSI em `OsFile`, `Workspace::Open`, `Settings`, `PathUtils::resolvePath` | `OsFile.cpp:6`, `Workspace.cpp:98` |
| 11 | **Filtro de tipos vazio em app por módulo** | `AssetVisibility::GetFilterableTypes` enumera o `TypeRegistry` legado, que nenhum módulo popula | `AssetVisibility.cpp:43` |
| 12 | **Renderer não é embutível** | `VkContext.cpp:362` sobrescreve a dispatch table global do volk com o device do Onyx; um segundo device no mesmo processo renderiza no device errado sem aviso (laudo do Icon-Composer, 4 commits pra contornar) | `VkContext.cpp:362` |

Menores mas reais: `catch(...)` só em Release envolvendo o frame inteiro
(`Window.cpp:894-912`, comportamento diferente em Debug) ⚠; reentrância
de `fullFrame` pelo refresh callback e pelo diálogo modal de Open ⚠;
`TaskManager::getRunningTasks()` devolve referência sem lock e tasks que
lançaram nunca são coletadas; `TypeCatalog::Register(forcedValue)` resize
sem limite e sem mutex; `TextureData::IsValid` só checa `!pixels.empty()`
mas o upload copia `w*h*4`; readback staging com flag de escrita
sequencial; `CmdExtract` retorna OK com entradas falhando;
`AssetReader` guarda u64 em double.

**Gaps conhecidos que o próprio CHANGELOG registra** e não fecharam:
HOST_COHERENT assumido sem `vmaFlush`; `DrawInspector()` sem caller (a
metadata do VideoPlayer e as checkboxes de visibilidade por batch são
inalcançáveis); redraw não-bloqueante nunca começou; macOS não suportado.

**CI:** o README diz que o job Linux "nunca executou". Falso: 31 runs no
remoto, **todos falhando** no Test (`jobs_test.cpp:131 weak.expired()` e
`VkOracleParity`). Windows 28/31 verde.

---

## 4. Por que é difícil de manter

### 4.1 Duas arquiteturas convivendo no `Include/`

| Legada (v0.x, "aposentada" em agosto) | v1 (viva) | Quem ainda usa a legada |
|---|---|---|
| `ITypeHandler` + `TypeRegistry` + `ONYX_REGISTER_FILE_TYPE` | `TypeCatalog` + `DecoderRegistry` | **GoWToolkit: 25 arquivos**; `ViewerRegistry.cpp` (sem `Register`, ninguém chama `Open`); `AssetVisibility` |
| `AssetContainer` / `Domain/Wad.h` / `Entry.h` | `Document` + `AssetTree` | **GoWToolkit: 25 arquivos** (+ `ContainerBridge.h` que o GoW escreveu pra pontear os dois, "escrito quatro vezes, uma com bug real") |
| `Api/ToolkitApi` (facade global, `abort()` se não inicializado) | `Workspace` | GoWToolkit: 5 arquivos |
| `Services/EventManager.h` | `Services/EventBus.h` | só `Window.cpp` |
| `Services/TaskManager.h` | `Services/Jobs.h` | GoWToolkit: 2; SDK: só pra subir threads ociosas |
| `App/TexturePool` | `Rendering/TexturePool` (que é um `DeferredDestroyQueue`) | ambos vivos, mesmo nome |
| `Schema/StructDef` + `NodeInstance` | `AssetFormat` (segunda DSL) | GoWToolkit: 2 (MDL do GOW2) |

O SDK não usa nada da coluna da esquerda. O consumidor usa quase tudo.
**Esta é a dívida central**: a migração v0→v1 parou na metade, dos dois
lados do muro, e cada camada do SDK carrega o custo de manter as duas.

### 4.2 God objects

| Arquivo | LOC | O que acumula |
|---|---|---|
| `Source/App/Window.cpp` | 1185 | GLFW + `VkContext` + swapchain + present + init do backend ImGui + `AppConfig` + `Workspace` + `App` + `SessionLog` + frame loop. ~600 linhas de Vulkan cru. Contrato de ordem de destruição é um comentário de 25 linhas (`Window.h:110-135`). |
| `Source/Viewers/Viewport3D.cpp` | 1153 | pipelines + renderer + offscreen target + texture pool + câmera + gizmo + inspector + transport de animação + timeline + leitura de config + registro global de animação ativa, tudo dentro de `Draw()`. |
| `Source/Viewers/VideoPlayer.cpp` | 990 | 3 threads FFmpeg + miniaudio + upload Vulkan + UI ImGui de transporte. Nunca instanciado pelo SDK, zero testes. Header público inclui libavformat/libavcodec/miniaudio/imgui. |
| `Source/App/SettingsWindow.cpp` | 758 | 203 chamadas raw ImGui vs 4 `Widgets::`; reimplementa a transição de cor do tema (`:76-120`) e chama `ApplyTheme` todo frame por 0,25 s; escreve `config->accent*` direto (dead stores). |
| `Source/Rendering/Pipelines.cpp` | 788 | 4 criadores copiados à mão (scene/grid/background/overlay); grid e background diferem em 24 de ~134 linhas; ~400 linhas removíveis com um builder. |

### 4.3 Hooks de host são ad-hoc

Seis `std::function` em `App.h` + três macros `ONYX_HAS_*` (o folder
picker não tem a dele), adicionados um por commit, **sem teste e sem
caller no repo**. `menuEntryWanted` só é aplicado a Close All, Recent,
Exit e Export, não a Open/Settings/View/Options/Reset Layout; chaves são
strings cruas; dispatch do open-handler duplicado em `App.cpp:201` e
`:396`. Labels anunciam Ctrl+, / Ctrl+E / Ctrl+C e nada está bindado. O
hook de visibilidade existe só porque `DocumentWindow` não é `IPanel`. O
7º e o 8º hooks já são previsíveis (veto de close: `m_wantClose` vai
direto pro `glfwSetWindowShouldClose`; file drop: callback vazio em
`Window.cpp:191`). Eles foram feitos pro Icon-Composer, que saiu mesmo
assim.

### 4.4 Duplicação CLI × GUI × headless

- `CmdDecode` copia a prioridade de `RouteForType` à mão
  (`Commands.cpp:285-345`), e o comentário diz que "não podem divergir".
- `JsonEscape`, `PrintDiags`, `FindEntryByName` copiados entre
  `Commands.cpp` e `Render.cpp` com a cópia justificada em comentário.
- Framing de câmera em 3 cópias (`Cli/Render.cpp:316-351`,
  `Viewport3D.cpp:277-313`, `Camera.cpp`), e a do CLI ignora
  `instanceTransform`/`flipZ`.
- `RenderToImage` duplica `Viewport3D::RenderFrame`.
- Euler→quaternion escrito duas vezes verbatim (`JointPalette.cpp:25-55`,
  `AnimationPlayer.cpp:~450`).
- `Cli/Gltf.h` é público mas a única definição vive em
  `Examples/OnyxCli/Gltf.cpp`: consumidor que inclui o header pega erro de
  link (GoWToolkit reimplementou e "reportou upstream").

### 4.5 Código sem caller no SDK

`IsoFileSystem`/`IsoFile` (ListDirectory stubado), `SliceFile`,
`AdpcmDecoder` (só VAG de PS2), `AssetContainer`/`Wad.h`, as duas DSLs
de Schema, `lz4_lib` (fetchado e linkado em Core, **nenhum source inclui
`lz4.h`**), `WadStatsView`, `Metrics`, `LoadFromMeshData`,
`ScaleManager`, `setFullFrameCallback`, `UploadAtlas` (só limpa flag, 2
callers), `WindowDecorator::drawTitleBar`, `ThemeManager::GetFlashState`,
`RenderContext::AddPass` (zero callers), o menu Export (3 itens sempre
desabilitados em todo host), `RenderBatch::gpuMesh` (aponta pra tipo que
não existe), enums Matcap/Wireframe/TexturedWire (no-op),
`isHighlighted` (nunca lido), `kTextureRoleCount=10` com 7 bindados.

**Mas:** ISO, lz4, Schema, Adpcm e VideoPlayer **têm** caller no
GoWToolkit (ver §4.1). "Sem caller no SDK" é sintoma de que o SDK não
testa o que o consumidor usa, não licença pra deletar.

### 4.6 Testes

283 casos em 56 arquivos, **todos** contra `OnyxBoxModule`. Zero testes
em: ISO, SliceFile, Adpcm, Schema, Api, ImageViewer, TextEditor,
VideoPlayer, TaskManager, SystemTheme, FontManager e ~30 arquivos de
Panels. Nenhum teste abre janela ou swapchain. `Tests/CMakeLists.txt`
roda a suíte inteira como `OnyxUnit` e de novo em ~40 subsets por glob.
Hotspots de churn: `CMakeLists.txt` 58 commits, `Tests/CMakeLists.txt`
52, `OnyxOracle/Main.cpp` 22, `Window.cpp` 20. 68 de 269 commits começam
com "fix".

### 4.7 Headers

60% comentário na camada de render (1277 de 2122 linhas), 41 comentários
são histórico de task tracker ("T10 fix-round-1", "F5 fix"); narrativa de
milestone dentro de header público; `PathUtils.h:10` inclui `<windows.h>`
sem `NOMINMAX` e `Onyx.h` o alcança (já há workaround pra colisão com
`TextOut` em `DecoderRegistry.h`); `volk.h` + `vk_mem_alloc.h` em quase
todo header de Render com regra de ordem de include; `Viewport3D.h`
esconde tudo atrás de pimpl porque `<windows.h>` quebrou o build;
`ImGuiCol` persistido como int cru (upgrade do ImGui corrompe tema salvo);
comentários em português no VFS e Schema.

---

## 5. O que os consumidores dizem

| Consumidor | O que usa | O que arrasta | O que fez |
|---|---|---|---|
| **GoWToolkit** (único consumidor completo, 16.135 LOC) | Quase tudo. 83% do código é formato/decoder (cumpre a promessa), 13% é glue de UI (não cumpre). | Tudo, por default. | Pinou `v1.1.0` antes da tag existir; 3 repins; `ContainerBridge` entre `Document` e `AssetContainer`; dois catálogos de tipo populados em paralelo; reimplementou `Gltf.h`; `--gui <file> <entry>` à mão com statics vazados; tipos mintados pelo módulo "ainda não aparecem em nenhum AssetEntry"; teste de contraste de tema quebrou por mudança no engine. |
| **Icon-Composer** (saiu em 30/09, commit `a7016a4`) | ~10 símbolos: `Window`, registrar/layout, `IPanel`, `MarkMainThread`, `SystemOpenFolderDialog`, `Progress`. | "O OnyxSDK inteiro — tema, settings, registrar, a barra de menus DELE por cima da nossa — e o editor usava uma fração e brigava com o resto." Precisou de `ONYX_COMPONENT_MEDIA` pra não baixar FFmpeg, hack de policy do CMake 4, strip de warning flags ("~700 linhas de ruído"), copiar fontes pro lado do exe. | 4 commits de dispatch table por device pra contornar o volk global. Laudos: `File > Exit` chama `exit(0)` pulando scheduler e `vkDeviceWaitIdle`; "Close All" no-op; Recent com tag GOW1/GOW2/GOWR; Export "que nunca teve corpo"; `SystemOpenFileDialog` "NÃO CONSEGUE devolver um diretório". Reescreveu 1.270 linhas (Shell 387, NativeWindow 187, TexturePool 255, JobQueue 105, Dialogs 183, Theme 155). Zero `#include <Onyx` hoje. |
| **SF-Symbols** (ejetou a Shell em 01/10, manteve a dep) | 3 serviços triviais: `Frame::RequestRedraw`, `MarkMainThread`, `DetectSystemAppearance`, mais `imgui_lib` compilado. | `Onyx::Onyx` linka opengl32 publicamente e puxaria "~60 TUs, GLFW, ImPlot, ColorTextEdit" nos testes, então os testes recusam linkar. | Patcha a propriedade SOURCES de `Onyx_Core` pra remover `SessionLog.cpp` (listado mas não commitado no pin). "Onyx's menu bar, dock tab headers and Viewer/Log panels — none of that can be turned off from outside… So the window loop is OURS now." |

**Leitura:** a camada que os consumidores rejeitam é exatamente a que o
design de agosto decidiu ser o produto ("Framework — SDK owns window,
dock, lifecycle"). O que entrega é a camada de dados + módulo + CLI.

---

## 6. Direção proposta

Princípio único, derivado da evidência acima:

> **O Onyx vira uma biblioteca em camadas que o host compõe. A janela
> pronta continua existindo, mas como a camada mais alta e opcional, não
> como o dono do processo.**

Isso inverte a decisão "Framework" de agosto. A justificativa é §5: o
único consumidor que aceitou o framework é o do próprio autor, e os dois
externos pagaram pra sair. Um SDK para ferramentas de jogos precisa poder
ser embutido num editor que já tem janela, já tem ImGui, já tem Vulkan.

### 6.1 Camadas (cada uma um target que compila e testa sozinho)

```
onyx::core      VFS (IFile stateless com ReadAt; nada de cursor), TypeKey/
                TypeCatalog como OBJETO do Workspace (sem Get()), IGameModule,
                Workspace, DecoderRegistry aberto (Scene/Image/Text/Audio/
                Table/Blob + registro de kinds novos por string), Jobs, EventBus,
                DiagSink, Settings (save atômico + periódico), Logger injetável.
                Data model genérico: Mesh/Material/Skeleton/AnimClip (keyframes,
                não bit-stream), Texture, Audio. Campos de jogo vão para
                ModuleState ou extensões tipadas por TypeKey.
                PROIBIDO: imgui, vulkan, glfw, windows.h, SFSymbols, libav.
                Teste: o LayerGuard vira um teste de compilação real
                (um TU por header, -I só do core).

onyx::gpu       Vulkan embutível. Aceita VkInstance/VkDevice/VkQueue EXTERNOS
                ou cria os seus; dispatch table por device (volkLoadDeviceTable
                ou tabela própria), nunca a global. RAII: Unique<T> + retire
                list por fence. UploadContext batched (um submit por Build,
                staging ring). PipelineBuilder + VkPipelineCache persistido.
                FrameRing com N frames in flight e recursos POR FRAME.
                Resultado: OneShot deixa de existir; o stall some junto.

onyx::render    SceneRenderer sobre gpu. UMA orquestração: viewport interativo
                e headless chamam a mesma RenderScene(target, scene, camera,
                overlays). Materiais deduplicados por hash. AnimationPlayer
                sobre AnimClip genérico (Q.14 e substreams vão pro módulo GoW).
                Vertex layout escolhido pelo renderer, não pelo parser.

onyx::ui        Widgets + panels como COMPONENTES chamados dentro do frame do
                host, no ImGui context do host. IPanel é o único contrato;
                DocumentWindow é um IPanel. ViewerRegistry aberto: viewer é um
                IPanel registrado por MediaKind/TypeKey, módulo ou host registra.
                Nenhuma janela, nenhum GLFW, nenhum main loop aqui.

onyx::shell     (opcional) A janela pronta: GLFW + swapchain + dock + menu +
                config + recents, montada SOBRE ui e gpu. Menu é um modelo de
                comandos (CommandRegistry por id, com atalho, enabled, visible)
                que o host sobrescreve/remove por id; os 6 hooks + 3 macros
                somem. Lifecycle delegate (willClose/didOpen/fileDropped). Window
                vira ~300 linhas de composição; Vulkan de swapchain mora em gpu.

onyx::media     (opcional) VideoPlayer dividido: MediaSource (FFmpeg, sem UI,
                sem Vulkan, testável) + MediaView (IPanel). Threading com um
                único owner do AVCodecContext.

onyx::cli       Mesmo Workspace; zero cópias de RouteForType/JsonEscape/framing.
onyx::exchange  glTF como hoje (já é limpo), mais PNG/WAV.
onyx::testkit   Como hoje, mais um segundo módulo sintético (ver 6.3).
```

### 6.2 O que sai do SDK

- Toda a coluna legada de §4.1: `ITypeHandler`, `TypeRegistry`,
  `AssetContainer`/`Wad`, `ToolkitApi`, `EventManager`, `TaskManager`,
  `App/TexturePool`, `Schema/StructDef`+`NodeInstance` (uma DSL só, se
  alguma). **Condição:** o GoWToolkit porta junto (ver 6.4); nada é
  deletado antes do consumidor compilar sem.
- `SFSymbols.h` vira um asset de fonte + tabela gerada em `onyx::ui`, não
  um header de 6686 linhas no Core.
- Nomes de jogo: `isRigid`, `meshHash`, `file0x20`, `vectors4..7`,
  `isQuaternion`, `flipZ=true`, `gameHint`, `GOWMenuItemHandler`, labels
  gow1/gow2/ragnarok, Q.14, substreams. Vão pro módulo GoW.
- `RenderContext::AddPass` e o "raw floor" como API separada: o raw floor
  passa a ser `onyx::gpu` inteiro, que é público por construção.
- Histórico de task em comentário de header (41 ocorrências) e narrativa
  de milestone.

### 6.3 Gates (o que prova que ficou bom)

1. **Core-only compila sem baixar nada de GPU/UI.** Um consumidor que
   linka `onyx::core` não vê glslang, GLFW, ImGui, Vulkan no configure.
2. **Embutível.** Um exemplo (`Examples/Embedded/`) com GLFW + ImGui +
   Vulkan próprios que hospeda um `Viewport3D` e um `DocumentBrowser` do
   Onyx sem usar `onyx::shell`. É o teste do Icon-Composer/SF-Symbols.
3. **Segundo módulo sintético** (o "COMI toy" cortado em agosto) com
   zero edits no SDK, incluindo um viewer próprio (ex. paleta) registrado
   pelo módulo. É o teste da promessa de agosto, finalmente.
4. **Headless == viewport.** `RenderToImage` e o viewport produzem o
   mesmo PNG para a mesma cena/câmera (golden único).
5. **Sem stall.** Teste que conta submits por `Build` (1) e por frame (0
   fora do present), e um teste de resize sob validation layers.
6. **Linux CI verde** (hoje 31 runs vermelhos).
7. **GoWToolkit compila e passa os goldens dele** contra a nova API, com
   `ContainerBridge`, o segundo catálogo e o `Gltf.h` reimplementado
   deletados.

### 6.4 Estratégia de migração: break-and-port, com o GoWToolkit como oráculo

Recomendo **v2 com quebra de API**, não strangler incremental:

- Há um consumidor real. O histórico v0.3→v1.1 mostra que churn
  incremental já custou 3 repins e uma ponte de compatibilidade no
  consumidor; manter as duas arquiteturas compilando durante uma
  migração lenta é exatamente o estado atual, que é o problema.
- O precedente funcionou: o GL foi oráculo do Vulkan e depois morreu. O
  GoWToolkit v1.1 + seus goldens são o oráculo do v2.
- A camada que não quebra (contrato de módulo, Jobs, EventBus, CLI, glTF)
  é movida, não reescrita.

Ordem sugerida, cada onda buildável e gateada:

| Onda | Conteúdo | Gate |
|---|---|---|
| W0 | Rebase de `feat/host-ui-hooks` sobre `origin/main` (8 atrás); decidir se os 4 commits de hook entram ou morrem na W3. CI Linux verde. | CI verde nos dois OS |
| W1 | `onyx::core` extraído: VFS stateless, TypeCatalog objeto, data model genérico, legacy fora, LayerGuard como teste de compilação. | Gate 1; GoW compila com shims temporários |
| W2 | `onyx::gpu` + `onyx::render`: RAII, UploadContext, PipelineCache, FrameRing, orquestração única, device externo. | Gates 4, 5; parity com os goldens de render atuais |
| W3 | `onyx::ui` + `onyx::shell`: Window decomposta, CommandRegistry, ViewerRegistry aberto, hooks removidos. Exemplo embutido. | Gate 2 |
| W4 | `onyx::media` dividido; CLI sem cópias; segundo módulo sintético; GoWToolkit porta e deleta as pontes. | Gates 3, 7 |

---

## 7. Decisões que são suas

Em ordem de quanto mudam o que se constrói:

1. **Framework ou biblioteca?** A proposta inverte a decisão de agosto.
   Se a resposta for "framework, a Shell é o produto", a arquitetura
   acima muda de forma: `onyx::shell` deixa de ser opcional e os gates 2
   e 3 caem.
2. **Break-and-port (v2) ou incremental?** A proposta é v2. Se for
   incremental, a coluna legada fica viva por mais um ciclo e o custo de
   W1 dobra.
3. **O que o GoWToolkit usa da coluna legada vira o quê?** ISO, lz4,
   Schema (MDL do GOW2), Adpcm (VAG), VideoPlayer, `ITypeHandler` em 25
   arquivos, `AssetContainer` em 25. Opções: (a) porta pro v2 dentro do
   SDK como componentes genéricos (ISO e lz4 são genéricos; VAG e o
   bit-stream do Go não são); (b) move pro módulo GoW; (c) mantém um
   `onyx::compat` por um ciclo. Minha recomendação: ISO, lz4 e Schema
   ficam no SDK e ganham teste; VAG, Q.14, substreams e os campos GOW vão
   pro módulo; sem compat.
4. **VideoPlayer dentro ou fora?** Dentro como `onyx::media` opcional,
   dividido, é o que proponho. Fora (lib separada) também é defensável:
   é o único lugar do SDK com FFmpeg, e o único consumidor é o GoW.
5. **Quem é o público do "ápice"?** Se é só o GoWToolkit, o gate 2
   (embutível) é luxo. Se é "SDK para ferramenta de games" no sentido
   amplo, o gate 2 é o mais importante de todos, porque é o que o
   Icon-Composer e o SF-Symbols precisavam.
6. **Os 4 commits de `feat/host-ui-hooks`** foram feitos pro
   Icon-Composer, que já saiu. Merge agora (e remove em W3) ou descarta?

---

## 8. Caveats deste levantamento

- Tudo é leitura estática. Nada foi compilado, rodado ou profilado nesta
  sessão. Os itens marcados ⚠ em §3 precisam de repro antes de virar bug
  fechado.
- O checkout local do GoWToolkit está num branch antigo
  (`feat/gowr-lod-rig-shaders`, pin v0.6.0); as contagens de §4.1 e §5
  foram tiradas de `origin/main` via `git grep`.
- `hooks/code_tree.py` referenciado pelo ambiente não existe no repo; o
  grafo de includes foi reconstruído por script próprio (apêndice api).
- Números de LOC e de call sites são `grep`/`wc`, não análise semântica.
