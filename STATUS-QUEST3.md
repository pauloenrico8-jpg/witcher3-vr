# Witcher 3 VR para Quest 3 — estado do desenvolvimento

**Ainda não existe uma versão jogável deste projeto com mãos e combate físico.**
O usuário cancelou a meta de 60 FPS em Novigrad em 06/10: o foco é concluir o VR,
com estéreo simultâneo, cabeça livre, mãos/armas Touch e combate físico.
Menções à meta nas etapas antigas são registros históricos. O arquivo compilado está na pasta de
desenvolvimento e não foi instalado no jogo.


## Disponibilidade e devolução da imagem VR — 14 de outubro

O caminho moderno ganhou confirmações separadas para receber, esperar, desenhar,
enviar e devolver uma imagem do headset. O código não considera a aquisição
sozinha como permissão de desenho.

O fluxo, passo a passo:

1. openxr_image_protocol.h liga a imagem à sessão, ao quadro, à fila gráfica e
   à cadeia de imagens. Guarda o índice retornado e o resultado original.
   Uma confirmação antiga ou de outra thread não altera a operação atual.
2. Depois da aquisição, espera pela disponibilidade. Timeout e falha deixam a
   imagem pendente, sem desenho nem devolução indevida. Perda de sessão permite
   apenas a devolução de uma imagem disponibilizada e não publica uma camada.
3. dxgi_proxy.cpp confere índice, duas posições de desenho, formato, dimensões
   e origem antes de acessar a textura. No OpenXR com DirectX 12, a espera
   bem-sucedida entrega a imagem de cor no estado de desenho. Por isso o caminho
   moderno não inventa uma transição inicial a partir de COMMON.
   [Regra oficial](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XR_KHR_D3D12_enable-swapchain-image-state.html).
4. Acompanha as três transições de cor conhecidas do host, exige retorno ao
   estado de desenho e envia pela fila conservada da sessão. Guarda a confirmação
   de envio e de seu sinal. Essa confirmação não significa que a GPU terminou.
5. Só devolve uma imagem disponibilizada, sem execução desconhecida. Uma lista
   que não chegou a ser enviada pode ser descartada sem presumir alterações
   executadas. Depois de envio, exige as confirmações correspondentes.
   A apresentação também exige sucesso na devolução.

Escolhi separar essas confirmações porque o tratamento anterior aceitava
timeout como disponibilidade, tentava devolver após falha de espera e apenas
registrava falha de devolução, sem impedir a apresentação.

A DLL compilou e **126 testes passaram em 13,08 segundos**. O novo teste fez
**148 conferências próprias** sobre índice, disponibilidade, transições de cor,
fila, sinal, devolução, pendências e retornos antigos. Os handles, retornos e
confirmações de estado foram **simulados**; não executou OpenXR real, callbacks
do host no Witcher, SDK, GPU ou Quest. A suíte gráfica própria anterior permanece.

**O VR ainda não funciona.** Remastered continua fechado; nada experimental foi
instalado. Em falha desconhecida, o código conserva a pendência e fecha novas
aquisições. A recuperação por encerramento e recriação ainda precisa de prova
de vida útil e conclusão real; não há recuperação automática por tempo.
A confirmação de transição cobre os três pontos conhecidos do host, não todos
os possíveis escritores externos. Este caminho trata imagem de cor; profundidade,
recursos do motor, encerramento completo e teste real continuam pendentes.

## Identidade conservada nas imagens dos dois olhos — 14 de outubro

O horário do desenho pode se repetir depois de reiniciar a sessão. Por isso,
o caminho moderno agora conserva também a sessão, o número de seu início e
o número exclusivo do quadro.

O fluxo, passo a passo:

1. xr_frame_identity.h guarda esses três números próprios. O coordenador
   fornece os números do quadro que realmente reservou; eles não são calculados
   a partir do horário ou do número de apresentações da janela.
2. dxgi_proxy.cpp leva a mesma identificação para a fábrica, os rótulos das
   tarefas, as cópias de imagem e o pedido de apresentação. Confere novamente
   nas entradas conhecidas de câmera e visibilidade, na cópia, na publicação
   e antes de enviar e encerrar. Uma sessão revogada não ganha outra identidade.
3. modern_eye_capture.h, modern_backbuffer_source.h e modern_eye_pair.h
   conservam a identificação copiada. Os dois olhos devem pertencer ao mesmo
   quadro e coincidir com o pedido do apresentador. Uma imagem de outro início
   de sessão é recusada mesmo se horário, geração e nome do par coincidirem.
   Tentativas já aceitas são consumidas uma vez; renomear o par não as renova.
4. modern_prepared_frame.h compara a identificação completa na reserva e no
   encerramento. Cancelamento ou retorno antigo não pode retirar a reserva
   de outro quadro com o mesmo horário.
5. modern_camera_history.h recomeça os quatro históricos ao mudar de início de
   sessão. Recusa entradas antigas mesmo com horários maiores; conserva a
   regra de geração e os números crescentes. O primeiro quadro da nova sessão
   recebe históricos inválidos próprios, preservando as regras anteriores.

Escolhi levar o identificador inteiro porque comparar apenas o horário deixava
a possibilidade de confundir uma imagem antiga com o novo quadro após reinício.
Os números comprovam correspondência dos registros próprios; não retêm objetos
do jogo nem demonstram pixels desenhados.

A DLL compilou; **125 testes passaram em 14,69 segundos**. Os testes de pares,
captura e histórico somam 49 conferências novas (totais 598, 59 e 1033,
respectivamente). Usaram dados e identidades VR **simulados**, incluindo mistura
entre olhos, reinício com horário igual, retorno antigo e reinício do histórico.
A suíte conservou o teste gráfico independente com objetos e cópias DirectX
próprios; seus identificadores de quadro continuam simulados.

**O VR ainda não funciona.** Não executou os callbacks do host no Witcher,
OpenXR real, SDK ou Quest. Remastered continua fechado e nada experimental foi
instalado. Ainda faltam estados e conclusão das imagens reais, continuidade do
produtor, recursos e encerramento completo, além das mãos e combate no jogo.

## Preparação e apresentação do mesmo quadro — 14 de outubro

O mod ganhou um coordenador próprio para as sessões modernas que já conservaram
a ligação gráfica. Ele controla quem prepara e quem apresenta um quadro, para
que duas chamadas simultâneas não substituam os dados uma da outra.

O fluxo, passo a passo:

1. openxr_frame_cycle.h reserva um quadro com identidade da sessão, número de
   início da sessão, número exclusivo do quadro e geração das imagens. Uma
   segunda preparação é recusada enquanto a primeira estiver em andamento ou
   o quadro estiver aguardando apresentação.
2. A espera do headset e o início/fim do quadro usam proteções separadas.
   Nenhuma chamada ao headset acontece segurando a proteção dos dados internos.
   Escolhi separar essas partes porque uma espera pode depender de um início
   ainda não executado. [Regra do OpenXR](https://registry.khronos.org/OpenXR/specs/1.1/man/html/xrWaitFrame.html).
3. Depois de localizar a cabeça, guarda cópias do horário e das duas visões.
   dxgi_proxy.cpp usa esse conjunto congelado na apresentação e na reserva da
   fábrica moderna. Só um apresentador recebe o quadro; outro não inicia uma
   nova espera para substituí-lo. A sessão registrada continua reconhecida
   mesmo se a permissão gráfica for revogada; não cai no caminho antigo.
4. Um quadro sem visão válida, sem permissão de desenho ou com resultado de
   descarte é encerrado sem camadas de imagem. Falhas e exceções fecham a
   sequência até um encerramento de sessão confirmado. Um resultado positivo
   inesperado não autoriza a continuação. [Início do quadro](https://registry.khronos.org/OpenXR/specs/1.1/man/html/xrBeginFrame.html).
5. Ao parar, recusa novas entradas e conserva as chamadas já admitidas até elas
   saírem. A consulta de eventos tenta encerrar a sessão depois dessa saída,
   sem ficar esperando enquanto segura uma proteção necessária às chamadas.
   Uma falha de encerramento não reabre a sessão. [Encerramento da sessão](https://registry.khronos.org/OpenXR/specs/1.1/man/html/xrEndSession.html).

A DLL compilou e **125 testes passaram em 13,33 segundos**. O teste novo fez
**232 conferências**, incluindo threads reais do programa próprio, preparação
bloqueada, apresentador simultâneo, parada, reinício, recusas, cópias e exceções.
Os horários, sessões e retornos da interface VR foram **simulados**. O teste de
ligação gráfica fez 54 conferências com objetos DirectX próprios e sessão VR
simulada; duas conferências novas verificam a classificação após revogação.

**O VR ainda não funciona.** Não executou callbacks do host no Witcher, OpenXR
real, SDK ou Quest. Nada experimental foi instalado; Remastered continua
fechado. Essa proteção cobre as entradas modernas ligadas ao coordenador.
Ainda falta levar o número exclusivo da sessão/quadro pelas tarefas do jogo
até as duas imagens copiadas, testar o caminho real e proteger o restante
das chamadas, recursos e encerramento. As referências e o contador de chamadas
não comprovam término da placa de vídeo nem permitem descarregar a DLL.

## Eventos e encerramento da sessão correta — 13 de outubro

Um aviso de mudança de sessão só pode alterar a sessão a que pertence. O mod
agora identifica essa origem antes de começar, encerrar ou atualizar a visão.

O fluxo, passo a passo:

1. `openxr_session_events.h` compara o aviso com a sessão atual e recusa sessões,
   tipos de aviso e estados desconhecidos. O início exige o estado READY; o
   encerramento exige STOPPING. São os estados definidos pela interface do
   headset para permitir essas chamadas. [Regra do OpenXR](https://registry.khronos.org/OpenXR/specs/1.1/man/html/xrEndSession.html).
2. `dxgi_proxy.cpp` usa essa decisão antes de alterar o foco das mãos e a sessão.
   Avisos de visibilidade também precisam corresponder à sessão, ao formato
   estéreo e a um dos dois olhos. O código de mudança de espaço já validava a
   própria sessão e foi preservado.
3. Depois da chamada de encerramento, conserva o resultado original. Se falhar,
   mantém o registro de que a sessão começou, mas recusa novos quadros enquanto
   ela estiver parando. Assim uma falha não vira uma confirmação de encerramento.
4. Ao receber perda ou saída da sessão, fecha a admissão de novos quadros.
   Conserva as referências gráficas; não presume que a placa de vídeo terminou
   nem destrói recursos ainda usados. A destruição completa continua pendente.

Escolhi essas verificações porque o tratamento anterior aceitava avisos de
qualquer sessão e tentava encerrar também nos estados de perda e saída, nos
quais essa chamada não está autorizada pela regra da interface.

A DLL compilou e **124 testes passaram em 15,3 segundos**.
O teste novo fez **112 conferências com avisos e resultados simulados**:
identidades diferentes, tipos e estados desconhecidos, falhas de encerramento,
admissão de quadros e máscaras dos olhos. Não executou OpenXR real, callbacks
do mod no Witcher, SDK, GPU ou Quest. Esses testes verificam a decisão própria.

**O VR ainda não funciona.** Remastered continua fechado; nada experimental
foi instalado. A política não coordena as threads nem interrompe uma chamada
que já esteja em execução. Ainda falta impedir a sobreposição indevida entre
preparação, envio e encerramento e provar dois olhos novos dentro do jogo.

## Fila gráfica conservada pela sessão — 13 de outubro

A sessão VR escolhe uma fila da placa de vídeo ao ser criada. Uma fila é o
caminho pelo qual o programa envia as tarefas de desenho. O mod agora conserva
essa ligação e confere se a apresentação ainda usa os mesmos objetos.

O fluxo, passo a passo:

1. `modern_xr_graphics_binding.h` guarda referências ao dispositivo, à fila e
   à cadeia de apresentação já identificados. Essas referências mantêm as
   interfaces disponíveis; elas não congelam os pixels nem provam desenho pronto.
2. `dxgi_proxy.cpp` fornece ao OpenXR esses mesmos dispositivos e fila concretos
   no caminho moderno. Só confirma a ligação depois do resultado original,
   sessão válida e conferência da origem. Uma falha impede rearmar essa tentativa.
3. Antes de adquirir imagens, executar a lista de tarefas e encerrar o quadro,
   confere a sessão e a ligação conservadas. Outra fila é recusada mesmo que use
   o mesmo dispositivo. Outra sessão, origem desconhecida ou cópia da ligação
   também não ganha permissão.
4. Um redimensionamento com os mesmos objetos pode conservar a ligação da
   sessão. A mudança da fila ou da cadeia exige um protocolo de encerramento;
   o código não redireciona uma sessão existente silenciosamente. As regras
   anteriores de geração, cópias e sinais continuam sendo exigidas.
5. A revogação fecha a admissão e conserva as referências. Isso não substitui
   a espera pelos sinais da placa de vídeo nem a proteção completa de todas as
   chamadas durante o encerramento do jogo.

Escolhi conservar a origem da sessão porque o envio vinha consultando a fila
atual, que pode mudar depois da criação. O OpenXR exige que a imagem devolvida
esteja disponível na fila definida para a sessão. [Regra oficial do OpenXR](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XR_KHR_D3D12_enable-swapchain-image-state.html).

A DLL compilou e **123 testes passaram em 13,78 segundos**.
O teste novo fez **52 conferências**, criando um dispositivo, duas filas DirectX
reais e uma janela própria oculta com apresentação DXGI. Exigiu recusa da outra
fila, falhas definitivas, sessões diferentes e conservação das referências.
Esse executável fica separado da DLL do mod e carrega as bibliotecas gráficas
pelos caminhos oficiais do Windows. Não carregou o mod nem inicializou o SDK.
Os identificadores e resultados do OpenXR, assim como a autorização de classe
nativa do helper, são simulados. Não executou callbacks do host, OpenXR real,
Witcher ou Quest e não desenhou os dois olhos do jogo.

**O VR ainda não funciona.** Remastered continua fechado e nada experimental
foi instalado. Ainda faltam a coordenação real das chamadas ao OpenXR, estados
das imagens, ordem GPU e duração dos recursos do jogo. Esses testes não liberam
a compatibilidade nem substituem duas imagens novas do jogo em cada quadro.

## Dois olhos vinculados ao quadro preparado — 13 de outubro

O envio das imagens agora exige o mesmo horário que o OpenXR forneceu ao
preparar o quadro. OpenXR é a interface usada para conversar com o headset.
Isso liga a pose da cabeça e as duas imagens à mesma preparação.

O fluxo, passo a passo:

1. A fábrica normal prepara um quadro e conserva seu horário. As marcas das
   duas câmeras carregam esse valor durante o desenho e a captura. O caminho
   moderno exige a preparação real; uma fábrica recusada não ganha outra rota.
2. `modern_prepared_frame.h` reserva esse quadro para um único par de olhos.
   Cada reserva recebe um número próprio. Uma chamada antiga não pode cancelar
   nem concluir a reserva de uma chamada mais recente.
3. `modern_eye_pair.h` compara os dois horários com o quadro que será enviado.
   Exige também olhos distintos, mesmo par e geração, cópias e sinais válidos.
   Confere novamente antes do envio e impede reapresentar um par já consumido.
4. Se o produtor falhar antes de encaminhar o par, cancela a reserva exata.
   A apresentação pode encerrar esse quadro com zero imagens. Encaminhar o par
   pelo processador ainda não significa que a placa de vídeo terminou o desenho.
5. O término confirmado do quadro libera a reserva. Uma falha conserva a recusa.
   Um encerramento de sessão confirmado limpa somente essa ligação e os dados
   emprestados do quadro; os números de reserva continuam avançando. Referências
   e cópias da placa de vídeo seguem suas regras de retenção próprias.

Escolhi ligar o horário e a reserva porque um par pode ser novo para o jogo e
mesmo assim pertencer a outra preparação do headset. A reserva também identifica
um cancelamento concreto, sem deduzir falha pelo tempo que passou.

A DLL compilou e **122 testes passaram em 13,2 segundos**.
O teste de apresentação fez **566 conferências com dados próprios**, incluindo
ambas as versões, horários diferentes, cancelamento antigo, falha no término e
reinício de sessão. Esses testes não executaram callbacks do mod dentro do jogo,
OpenXR real ou Quest. Eles não comprovam a produção das imagens no Witcher.

**O VR ainda não funciona.** Remastered continua fechado e nada experimental
foi instalado. Ainda faltam a coordenação real das chamadas ao OpenXR, a fila
usada pela sessão e a ordem e duração dos recursos do jogo e da placa de vídeo.
Somente depois de produzir duas imagens novas do jogo por quadro haverá teste
no Quest, seguido de mãos, armas, dano e bloqueio por contato físico.

## Centro de visão nos dados enviados aos efeitos — 13 de outubro

Os efeitos recebem uma tabela de números que relaciona a imagem atual à
anterior. O mod agora prepara uma cópia dessa tabela com o centro de visão do
olho correspondente, no caminho sem antialiasing temporal.

O fluxo, passo a passo:

1. `modern_effect_reprojection.h` confere a versão e os três formatos de envio
   examinados. Compara a tabela recebida com a câmera atual e verifica a fórmula
   usando a lente e a visão atuais e anteriores. Dados inválidos são recusados.
2. Retira o centro da imagem atual, aplica a transformação e acrescenta o centro
   anterior. Essa ordem mantém o ponto do mundo na posição certa após a mudança
   de câmera. A correção é preparada em memória própria.
3. `dxgi_proxy.cpp` exige a marca exata do olho e do quadro. O Windows recupera
   o descritor usado pela função de fora; ele precisa corresponder à câmera do
   quadro. Isso impede aceitar outra captura somente por ter valores iguais.
4. Confere novamente os dados e encaminha uma única chamada original. Só troca
   o ponteiro da cópia temporária nas três rotas conhecidas; identificadores,
   argumentos restantes e resultado original são preservados. As câmeras e
   os objetos do jogo não são modificados por essa correção.
5. A instalação tem uma tentativa e só publica prontidão depois da ativação.
   Em falha, conserva a ligação para encaminhar chamadas. Isso ainda não resolve
   a proteção de todas as chamadas durante o encerramento do jogo.

Escolhi corrigir o envio porque o jogo calcula essa tabela antes de acrescentar
os centros às projeções. Alterar o histórico inteiro nesse ponto poderia repetir
uma correção. Versão, origem, tamanho, tabela, continuidade e geração precisam
coincidir; em qualquer recusa, o envio segue com seus dados originais.

A DLL compilou e **122 testes passaram em 15,65 segundos**. O teste novo fez
**842 conferências** de pontos em perspectiva, três formatos de envio,
preservação dos demais bytes, sobreposição e recusas. O teste do Windows fez
**389 conferências em três threads**, incluindo a recuperação real do registro
adicional usado para identificar uma das câmeras. Funções, dados e pilha desses
testes são próprios. A matemática também foi conferida em um modelo privado.
Nenhum callback do host, instalação de hook no jogo, SDK, GPU ou Quest foi
executado nesta etapa.

**O VR ainda não funciona.** A seleção global continua igual e o caminho
Remastered permanece fechado. Nada experimental foi instalado. Ainda faltam
provar a execução desse caminho no jogo e a ordem e duração dos recursos que
produzem duas imagens novas por quadro, antes do Quest e do combate físico.

## Quatro históricos construídos em memória própria — 13 de outubro

Os dados da câmera anterior ajudam o jogo a comparar a imagem atual com a
anterior. Agora o mod prepara esses dados separadamente para as duas câmeras
internas de cada olho, em quatro espaços próprios.

O fluxo, passo a passo:

1. `modern_camera_history_math.h` calcula posição, orientação e perspectiva
   com os valores copiados. As matrizes são tabelas de números que transformam
   pontos do mundo em pontos da imagem. A fórmula conserva a ordem dos eixos
   e a profundidade específica desse registro anterior.
2. Antes de usar a fórmula, compara seu resultado com o registro da chamada
   original já observado. Posição, lente e tempo precisam ser iguais; os
   cálculos das matrizes permitem apenas a diferença de arredondamento
   explicitada no código. Nenhuma chamada extra ao construtor do jogo é feita.
3. `modern_camera_history_materialization.h` cria uma cópia temporária dos
   descritores privados dos dois olhos. Reconstrói cada histórico anterior
   usando o tempo atual e acrescenta o centro de visão correspondente.
4. Em um reinício, copia o registro inválido original. Uma falha em qualquer
   câmera conserva ambos os descritores. Só devolve o conjunto completo quando
   os quatro resultados foram preparados, preservando referências e espaços
   de preenchimento que não fazem parte dos campos definidos.
5. `dxgi_proxy.cpp` liga essa preparação ao fluxo da fábrica e confere novamente
   origem, relógio e geração antes de fornecer as cópias. Uma recusa mantém a
   entrada normal do jogo e cancela a duplicação correspondente.

Escolhi cálculo próprio porque evita novas chamadas às rotinas matemáticas do
jogo. A comparação com a chamada original é uma condição de compatibilidade,
mas ainda precisa ser executada no jogo. Um resultado numericamente compatível
não comprova a duração dos objetos, a ordem dos desenhos ou imagens corretas.

A DLL compilou e **121 testes passaram em 13,51 segundos**. O teste novo fez
**126.795 conferências** com matrizes analíticas e dados próprios; a maior parte
confere byte por byte a preservação dos dois descritores. Inclui profundidade,
eixos, rotações, quatro históricos, reinício e saídas conservadas em falhas.
A primeira compilação falhou por um tipo de ponteiro no laço; o tipo foi
corrigido e a compilação final passou. Não houve execução do jogo, dos callbacks
do host, de SDK, GPU, OpenXR ou Quest nesta etapa.

**O VR ainda não funciona.** Nada experimental foi instalado; o caminho
Remastered permanece fechado. A preparação altera somente as cópias privadas.
Ainda faltam a conferência dos consumidores desses históricos e os recursos,
a ordem e a duração das operações necessárias para duas imagens novas do jogo
por quadro. Depois vêm o teste no Quest e mãos, armas e combate por contato.

## Matriz padrão observada na construção da câmera — 13 de outubro

A matriz padrão é o conjunto de valores que mantém uma transformação sem
introduzir deslocamento ou rotação. O observador agora confere o valor usado
pelo construtor da câmera, no mesmo fluxo em que acompanha sua origem e tempo.

O fluxo, passo a passo:

1. `modern_camera_defaults.h` aceita somente a versão conhecida e os dois
   pontos de leitura examinados. Lê os valores para uma cópia própria e os
   compara com a matriz padrão esperada, repetindo as leituras.
2. `dxgi_proxy.cpp` guarda essa cópia antes da chamada original ao construtor.
   Depois da chamada, confere os valores novamente junto com a origem e o
   relógio que já acompanhava. Uma mudança detectada recusa a observação.
3. Quando a fábrica usa os dados anteriores, a matriz é conferida outra vez.
   Uma falha nessa conferência revoga a observação, sem aceitá-la mais tarde
   como se a leitura tivesse sido válida.
4. A leitura é protegida contra falhas de acesso. O observador conserva a saída
   anterior quando um endereço é inválido ou a página está inacessível.
   A chamada original ao construtor continua
   recebendo os mesmos argumentos e devolvendo seu resultado.

Escolhi observar o valor realmente consumido porque identificar sua rotina de
inicialização no arquivo não garante o estado da memória naquele momento.
O observador não inicializa nem modifica essa matriz. Duas leituras iguais
não são uma trava contra alterações concorrentes nem autorização para chamar
um construtor adicional.

A DLL compilou e **120 testes passaram em 13,95 segundos**. O teste novo fez
**65 conferências**, incluindo cada valor alterado, falha em cada leitura,
versão desconhecida, saída preservada e páginas próprias do Windows tornadas
inacessíveis ou não comprometidas. O mesmo leitor protegido é usado pelo host
e pelo teste. Os endereços e dados de teste são próprios; nenhum código do
jogo foi executado nessa verificação.

**O VR ainda não funciona.** Nada experimental foi instalado. A seleção global
continua restrita à versão anterior, e o caminho Remastered permanece fechado.
Esta etapa não chama o construtor extra nem escreve históricos nas câmeras.
Ainda faltam as dependências restantes da construção, seus consumidores e a
validação de duas imagens novas do jogo por quadro, antes de testar no Quest
ou integrar mãos, armas e combate físico.

## Centro de visão nos quatro históricos próprios — 13 de outubro

Cada olho pode ter um centro de visão diferente, mesmo quando a posição e a
lente já estão corretas. A reconstrução dos dados anteriores agora conserva
essa diferença nos quatro resultados temporários, dois por olho.

O fluxo, passo a passo:

1. `modern_camera_history_projection.h` recebe um registro novo, produzido no
   armazenamento do mod. Confere a versão conhecida e a forma da projeção em
   perspectiva, que faz objetos distantes parecerem menores.
2. Acrescenta o deslocamento horizontal e vertical do olho. Altera somente
   dois valores da projeção; profundidade, orientação, posição, lente, tempo
   e demais bytes são preservados.
3. Recusa registros inválidos, valores não finitos, outra versão, projeção
   incompatível ou um resultado já deslocado. Assim a correção não se acumula.
4. `modern_camera_history.h` aplica a correção a cada resultado antes de
   devolver os quatro juntos. Uma falha conserva toda a saída anterior.
   Reinícios continuam copiando o registro inválido, sem chamar o construtor
   nem corrigir sua projeção.

Escolhi corrigir apenas o resultado novo em memória própria porque cada olho
precisa do seu centro e o registro anterior do jogo não deve ser alterado em
partes. Os testes multiplicam pontos pela projeção e conferem o deslocamento
após a divisão pela profundidade, além de preservar os bytes restantes.

A DLL compilou e **119 testes passaram em 8,43 segundos**. O teste de histórico
fez **1.023 conferências** com dados e construtor C++ próprios, incluindo centros
positivos e negativos, pontos em várias profundidades, recusa de correção
repetida e falhas em cada um dos quatro resultados. Não executou matrizes
nativas do jogo, chamadas do host, SDK, GPU ou uma sessão OpenXR no Quest.

**O VR ainda não funciona.** O host continua guardando somente entradas e não
chama a reconstrução extra nem escreve os quatro históricos nas câmeras do
jogo. O construtor nativo também consulta estado global inicializado em
execução: essa dependência e os consumidores normais ainda precisam ser
verificados antes da ligação. O bloqueio Remastered permanece e nada foi
instalado. Depois de comprovar duas imagens novas do jogo por quadro vêm os
testes de cabeça no Quest e a integração de mãos, armas e combate físico.

## Dados anteriores separados para os quatro históricos — 13 de outubro

A preparação passa a guardar quatro conjuntos próprios de dados anteriores:
um para cada câmera interna do motor em cada olho. São valores copiados;
nenhum endereço de objeto do jogo é conservado nesse armazenamento.

O fluxo, passo a passo:

1. O par já preparado fornece os quatro conjuntos de posição, orientação e
   lente. A origem de recentralização e as opções são congeladas junto com o par.
2. `modern_camera_history.h` conserva apenas os dados de entrada, sem guardar
   um registro pronto de um quadro antigo. Confere geração, par, origem nativa,
   lente, recentralização e relógio antes de aceitar a continuidade.
3. Um reinício nativo ou mudança nesses dados encerra a continuidade dos quatro
   conjuntos. Um par antigo ou já recusado não pode ser aceito novamente.
4. A função de reconstrução prepara quatro resultados temporários no tempo
   atual recebido. Só devolve a saída depois de todos terem sucesso. Falhas de
   retorno, tempo ou validade conservam a saída anterior. Reinícios não chamam
   esse construtor. A função foi testada com um construtor C++ próprio.
5. No fluxo do motor, os valores são reservados antes das fábricas. A reserva
   só é confirmada como entrada do processador após o despacho do par. Falhas,
   cancelamento ou fim da chamada abandonam a reserva exata; uma resposta antiga
   não pode retirar a reserva nova. Esse despacho não comprova imagens ou GPU.

`modern_temporal_source.h` entrega uma cópia dos valores observados;
`modern_camera_history.h` organiza continuidade, reinício e resultados;
`dxgi_proxy.cpp` liga a reserva ao fluxo existente. Escolhi guardar entradas
para reconstruir no tempo atual, preservando dados independentes por olho e
câmera. As duas câmeras internas continuam pertencendo a uma cena de um olho.

A DLL compilou e **119 testes passaram em 15,50 segundos**. O novo teste fez
**98 conferências** sobre dados fabricados, incluindo cópias independentes,
quatro chamadas próprias, falhas em cada resultado, tempo incorreto, reinício,
reserva alterada e respostas antigas. O teste de pose também usa apenas tipos
OpenXR e matemática própria; não abriu uma sessão no headset.

**O VR ainda não funciona.** No host do jogo, esta etapa guarda os dados e
não chama o construtor extra nem escreve os quatro históricos nas câmeras.
Ainda é necessário comprovar a permissão e vida útil da construção nativa,
portar suas matrizes e centros de projeção, ligar os históricos e verificar
recursos e duas imagens novas do jogo por quadro. Nada experimental foi
instalado e o bloqueio das versões Remastered permanece. Depois vêm Quest,
cabeça, mãos, armas e dano, bloqueio e aparo por contato físico.

## Reinício do histórico sem perder os valores da chamada — 13 de outubro

O motor tem um caminho que reinicia o histórico da câmera sem chamar seu
construtor de histórico. O mod agora observa a cópia desse reinício e a vincula
à mesma cena e ao mesmo objeto do jogo, em vez de exigir uma construção que
não aconteceu ou inventar dados anteriores.

O fluxo, passo a passo:

1. A ponte em `temporal_record_leaf_bridge.asm` chama a cópia original primeiro,
   uma vez. Depois conserva os valores deixados pelo original, antes de chamar
   a observação em C++. Ao sair, devolve esses valores ao chamador.
2. Isso inclui sete registros inteiros, todos os dezesseis registros de vetor
   e os indicadores do processador. São pequenas áreas de trabalho que uma
   função normalmente pode mudar; essa rotina específica deixa valores ali
   que outra parte do motor ainda usa. Seu corpo inteiro é conferido antes
   da instalação porque a ponte depende desse comportamento específico.
3. A observação exige a chamada de reinício, a origem nativa exata, o descritor
   original, o objeto do relógio e a câmera normal correspondentes. Confere
   os campos do registro copiado e os valores da mesma chamada do processador.
4. `modern_temporal_source.h` conserva o recibo do reinício como caso separado.
   Ele não fornece dados de câmera anterior nem autoriza um tempo para chamar
   o construtor. A fábrica compara esse registro com o que recebeu do motor.
   Uma falha encerra o recibo; uma segunda observação não o substitui.
5. A instalação só anuncia prontidão depois de criar e habilitar a ligação.
   Uma instalação parcial conserva o original e não tenta novamente sozinha.
   Isso ainda não demonstra que a DLL pode ser descarregada com segurança.

Escolhi uma ponte de observação depois do original porque uma função C++ comum
poderia apagar o valor de um registro que a cópia seguinte ainda utiliza.
O arquivo de definição `.h` descreve apenas o espaço próprio da observação,
sem presumir o retorno de outras funções do motor. As cópias de câmeras privadas
e alternativas continuam encaminhadas e não ganham essa prova de reinício.

A DLL compilou e **118 testes passaram em 12,03 segundos**. O teste novo fez
**871 conferências**, com 96 interceptações reais de uma função própria,
192 chamadas próprias e três threads. Comparou os registros e indicadores
com a mesma função sem a observação; também percorreu a pilha real do Windows.
O teste de relógio e recibos fez **985 conferências**, com 211 novas sobre dados
fabricados de reinício, identidades, recusas permanentes e campos do registro.
Esses testes não executaram a cópia, câmera ou instalação no Witcher.

**O VR ainda não funciona.** Nada foi instalado no jogo, nenhuma imagem nova
do jogo foi produzida e o bloqueio das versões Remastered permanece. O próximo
passo é manter os quatro conjuntos de dados anteriores das câmeras por olho,
respeitando reinício, continuidade e geração, e reconstruí-los no tempo nativo
comprovado. Permanecem pendentes as imagens, recursos e parada segura do motor.
Depois de duas imagens reais por quadro no PC vêm Quest, cabeça, mãos, armas,
dano, bloqueio e aparo por contato físico. DLSS continua opcional.

## Origem e tempo do histórico ligados à construção nativa — 13 de outubro

O mod passa a observar os dados anteriores no momento em que o jogo os usa
para construir o histórico. Ler esses dados apenas na fábrica seria tarde:
o jogo já teria substituído a câmera anterior pela câmera atual.

O fluxo, passo a passo:

1. A função de construção de histórico que já era interceptada continua
   chamando o original uma vez, com seus nove argumentos e retorno preservados.
2. Antes dessa chamada, confere a chamada interna exata, o construtor da cena,
   o objeto do jogo, sua interface de câmera e o relógio. Conserva uma cópia
   dos dados anteriores e compara os valores recebidos pela construção.
3. Depois do original, confere o resultado, o tempo gravado e a estabilidade
   dos dados. O registro conservado serve para conferir essa construção atual;
   não é reutilizado como histórico de um quadro anterior.
4. Na fábrica e depois da primeira câmera, exige que o histórico recebido
   corresponda à construção observada no mesmo objeto, chamada e tempo.
   Falhas encerram a observação, que não pode ser copiada ou reativada.
5. A instalação dessa observação na atualização atual tem sua própria
   conferência e independe de DLSS. Criação parcial não publica prontidão;
   os originais são conservados e não há nova tentativa implícita.

`modern_temporal_source.h` confere os argumentos, conserva os dados e compara
os campos definidos do resultado; `modern_factory_clock.h` e seu leitor Windows
separam explicitamente construtor e fábrica; `dxgi_proxy.cpp` liga essas regras
à chamada que já existia. Escolhi observar essa origem para não tomar um tempo
global, vindo de outra câmera ou outro quadro, como prova de continuidade.
Os campos de preenchimento do registro são preservados e ignorados na comparação.

A DLL compilou e os **117 testes passaram**. O teste específico fez **774
verificações**, com 449 novas: chamadas distintas, argumentos, mudanças de
identidade, todos os bytes do registro, recusas permanentes e uma chamada C++
própria com nove argumentos. Esses dados e resultados foram fabricados; não
executaram o construtor, o relógio ou a instalação dessa observação no Witcher.
Os testes Windows próprios anteriores continuam na suíte. Nada foi instalado
no jogo e o bloqueio global das versões Remastered permanece.

**O VR ainda não funciona.** Essa etapa não constrói os quatro históricos de
olho, não desenha imagens nem prova vida útil dos objetos ou parada segura da
DLL. Câmeras alternativas e o caminho nativo de reinício ainda não recebem essa
prova; sem ela, a duplicação é recusada e a chamada original é encaminhada.
O próximo passo é conferir esse reinício e construir os quatro históricos com
dados anteriores próprios e o tempo atual comprovado, antes de validar duas
imagens novas do jogo por quadro. Depois vêm cabeça, mãos, armas e combate físico.

## Relógio e interface de câmera do mesmo objeto — 12 de outubro

O observador do relógio agora confere também de onde veio a câmera. O descritor
original precisa apontar para a mesma janela de renderização guardada pelo
objeto que fez a chamada. Essa janela precisa apontar para a interface de
câmera desse objeto, com a classe e a função de consulta correspondentes.

O fluxo, passo a passo:

1. Recupera o chamador e o descritor original pelo Windows, como antes.
2. Lê a janela indicada pelo descritor e pelo objeto do relógio; exige igualdade.
3. Segue a ligação da janela para a interface de câmera. Confere a tabela de
   funções da interface separada e sua função de consulta. A tabela principal
   do objeto contém outra função nessa mesma posição e é recusada nessa ligação.
4. Só então lê contador e escala. Repete toda a leitura e conserva os valores
   durante a mesma chamada do processador. Uma ligação diferente encerra a
   observação e não volta a ser aceita nessa chamada.
5. As conferências que já aconteciam antes do par, antes do preparo das poses e
   depois da primeira câmera passam a exigir também essas ligações inalteradas.

`modern_factory_clock.h` define as correspondências e recusas;
`modern_factory_clock_windows.h` faz as leituras protegidas;
`modern_factory_clock_tests.cpp` confere o comportamento. Escolhi incluir a
ligação no observador existente para que relógio e câmera não recebam provas
separadas que poderiam pertencer a objetos diferentes. Nenhuma função da
interface é chamada, nenhuma referência do jogo é alterada e nenhum histórico
nativo é escrito por esta etapa.

A DLL compilou e os **117 testes passaram**. O teste do relógio fez **325
conferências**, incluindo 207 novas. Além dos dados fabricados de classe e
chamador, executou o leitor protegido do Windows sobre memória própria: ligação
correta, receptor errado, função trocada e página inacessível. Os casos recusados
conservaram o resultado de saída intacto. Isso valida o leitor e as recusas em
um programa próprio; não valida a vida útil ou sincronização dos objetos do jogo.
Duas leituras iguais também não demonstram que um objeto não foi destruído e
recriado entre elas.

**O VR ainda não funciona.** Não foram executados Witcher, SDK, OpenXR ou Quest;
o bloqueio global das versões Remastered permanece e nada foi instalado no jogo.
Ainda é preciso ligar continuidade e reinicialização das câmeras ao instante
correto do motor, construir os históricos por olho e comprovar duas imagens
novas do jogo em cada quadro. Depois vêm cabeça, mãos, armas e combate físico.

## Resultados do produtor preservados após a observação — 12 de outubro

A função do jogo que prepara a cena estava ligada a uma função C++ declarada
como se não devolvesse resultado. O significado exato do resultado ainda não
foi comprovado. Agora uma ponte em Assembly, instruções diretas do processador,
conserva os valores recebidos e devolvidos sem convertê-los para um tipo escolhido
pelo mod. Isso também protege o resultado das operações feitas ao encerrar a
observação da cena.

O fluxo, passo a passo:

1. Na entrada, a ponte conserva os quatro espaços de argumentos inteiros e
   quatro espaços de argumentos vetoriais do padrão Windows de 64 bits.
2. A parte C++ mantém a observação do produtor que já existia. A ponte repõe
   os argumentos originais e chama a função original uma vez.
3. Assim que ela termina, conserva seus resultados inteiros e vetoriais,
   inclusive os bits superiores dos vetores, antes de qualquer limpeza do mod.
4. Depois de encerrar a observação, repõe esses resultados e retorna ao
   chamador. Nenhuma conversão de número, ponteiro ou valor fracionário ocorre.
5. Os metadados das duas funções Assembly permitem que o Windows percorra
   suas chamadas. A instalação exige suporte AVX no processador e no Windows;
   esse recurso conserva os vetores completos usados nessa rota do jogo.

`engine_producer_bridge.asm` faz a passagem dos dados;
`engine_producer_bridge.h` define a disposição do registro temporário;
`dxgi_proxy.cpp` conserva o mesmo período de observação em C++. Escolhi separar
essa passagem porque uma operação de limpeza pode modificar os espaços usados
para devolver um resultado, mesmo quando a função original acabou corretamente.
A ponte cobre os argumentos em registros desta rota conhecida; não transporta
argumentos adicionais na pilha e não declara uma assinatura universal do jogo.
O endereço da função original fica opaco, sem uma chamada C++ que presuma seu
retorno. A instalação parcial conserva os originais conforme a regra existente.

A DLL compilou e os **117 testes passaram**. O teste próprio fez **697
conferências, 100 chamadas originais e 96 passagens pela interceptação real**,
em três threads. MinHook, a ferramenta que desvia a chamada, atuou somente
sobre uma função criada para esse teste. Um alvo Assembly independente leu os
argumentos efetivos e devolveu os bits prescritos; outra função alterou os
registros antes e depois da chamada para verificar a restauração. Valores de
NaN, infinito, zero negativo e partes superiores dos vetores foram conservados.
O mesmo código da ponte foi usado pelo teste e pela DLL.

**O VR ainda não funciona.** Não foi executado o jogo, seu produtor, SDK,
OpenXR, placa de vídeo ou Quest nesta etapa. Conservar o resultado do
processador não prova a vida útil dos objetos apontados, retirada segura da
DLL ou sincronização do motor. O gate global das versões Remastered continua
fechado e nada experimental foi instalado no jogo.

O próximo passo é ligar os dados anteriores das câmeras ao instante correto do
jogo, com sua ordem de atualização, invalidações e continuidade comprovadas.
Ainda faltam recursos e históricos por olho e duas imagens novas do jogo em
cada quadro; depois vêm as mãos, armas e o combate por contato físico.

## Relógio ligado à chamada normal da câmera — 12 de outubro

O preparo das duas câmeras da atualização atual passa a conferir o relógio da
mesma chamada que forneceu os dados de câmera. O relógio apenas identificado
por seu endereço global não basta: ele precisa corresponder ao objeto que fez
a chamada e continuar com os mesmos valores durante o preparo do par.

O fluxo, passo a passo:

1. Na entrada da função que cria os dados da cena, conserva o endereço do
   chamador e o argumento original, antes de preparar cópias para os olhos.
2. O Windows percorre a pilha, isto é, o registro das funções ainda em execução.
   Seus próprios metadados restauram os registros do chamador correto; o código
   não toma os registros atuais da função de observação como dados do jogo.
3. Confere a versão, trechos de código, a cadeia de metadados, a chamada exata
   e os limites do argumento na pilha. Um fragmento de função é conferido como
   fragmento; não é confundido com o início principal da função.
4. Só lê o relógio quando seu objeto coincide com o chamador recuperado e sua
   classe corresponde ao perfil conhecido. Repete as leituras, conserva o
   contador e a escala e calcula o tempo na mesma ordem de conversões do jogo.
5. Antes de preparar o par e depois da primeira câmera, confere novamente o
   objeto, contador, escala, versão e thread, a linha de execução do processador.
   Uma mudança encerra a observação. Essa recusa não é reaberta pela volta aos
   valores anteriores e impede admitir a duplicação nessa rota.
6. Essa observação existe somente dentro da chamada atual. Conserva valores
   simples de tempo e não concede posse do objeto, conclusão da placa de vídeo
   nem permissão para construir os históricos nativos da câmera.

`native_caller_context.h` recupera o contexto pelo Windows;
`modern_factory_clock.h` confere a identidade e conserva o tempo;
`modern_factory_clock_windows.h` faz as leituras protegidas;
`dxgi_proxy.cpp` liga as conferências ao preparo do par. Escolhi essa divisão
para testar a regra sem objetos do jogo e testar a recuperação pelo Windows
com o mesmo código usado pela DLL. O perfil de chamador desta etapa cobre
somente a atualização atual; outras versões não recebem esse perfil por cópia
ou por um deslocamento de endereços.

A DLL compilou e os **116 testes passaram**. O teste próprio de Windows fez
**380 conferências, nove chamadas e três threads**, com cadeia de cinco partes e
salvamento tardio de um registro. Outros **118 testes de condição** usaram dados
fabricados para conferir recusas e mudanças do relógio. Nenhum deles executou
essa leitura dentro do Witcher, o SDK, OpenXR ou o Quest. O bloqueio global das
versões Remastered permanece; nada experimental foi instalado no jogo.
**O VR ainda não funciona.**

Próximo passo: comprovar a vida útil e a sincronização do relógio na chamada do
jogo, depois reconstruir os dados anteriores de cada câmera com o tempo atual
admitido. Continuam pendentes os recursos e históricos por olho, a ordem de
produção e a entrega de duas imagens novas do jogo em cada quadro. As mãos,
armas e o combate por contato físico vêm depois dessa comprovação.

## Funções originais separadas por rota — 12 de outubro

A captura moderna agora guarda suas próprias funções originais do DirectX.
Antes, a mesma variável podia conter uma função de um intermediário do SDK e
ser usada por uma chamada nativa. Esses objetos precisam receber a função
correspondente à interface que realmente fez a chamada.

O fluxo, passo a passo:

1. A instalação moderna usa quatro funções próprias: iniciar uma gravação,
   enviar listas à placa de vídeo, fechar uma gravação e mudar o estado de uma
   textura. Seus destinos e funções originais ficam separados dos intermediários.
2. Se o projeto já instalou uma dessas funções nativas, só a reutiliza com a
   comprovação do mesmo destino, mesma função original e ativação concluída.
   Uma função apenas criada, um intermediário ou uma ligação parcial não basta.
   Uma falha conserva os originais; não remove, troca ou tenta instalar de novo.
3. Cada chamada encaminha os mesmos objetos, quantidade, array e retorno à
   função original de sua rota. No modo moderno, o intermediário apenas encaminha;
   a observação pertence ao ponto nativo mais profundo. Assim a identidade do
   intermediário não recebe a época de gravação da lista nativa.
4. Duas operações internas que chamavam diretamente uma função de mudança de
   estado agora conferem o método da lista recebida. Usam a função conservada
   para aquele destino exato; uma associação desconhecida ou ambígua não modifica
   a textura. A chamada normal do jogo continua com seu original correspondente.
5. Os corpos comuns recebem a função original como argumento explícito. O
   endereço do chamador também é capturado na entrada real e passado adiante,
   preservando a distinção existente dos comandos internos do RenderDoc.

`modern_hook_original.h` conserva as associações de destino e original;
`dxgi_proxy.cpp` separa instalação e encaminhamento; os testes conferem a seleção,
publicação entre threads e a preservação das chamadas. Escolhi guardar as duas
rotas porque obter uma interface nativa não transforma uma função do intermediário
em uma função nativa. Essa associação não prova, sozinha, ativação da ligação,
vida útil do motor, conclusão da placa de vídeo ou retirada segura da DLL.

A DLL compilou; **114/114 testes passaram em 9,79 segundos**. O programa gráfico
separado realizou **62 cópias reais com pixels conferidos**. Nele, as quatro
funções do DirectX foram interceptadas de verdade; a seleção usou o método real
recebido e o mesmo original conservado. Reutilizou três ligações nativas existentes
e instalou a quarta. Quantidade e array de duas listas ficaram intactos. Os
intermediários do teste CPU são simulados; esse teste não executa o SDK. Falhas de
sinal, alocação e ativação dos cenários anteriores continuam artificiais.

**O VR ainda não funciona.** Essa rodada não inicializou o SDK nem executou o
Witcher, suas câmeras, OpenXR ou Quest. O teste real dos intermediários com os
quatro observadores ainda falta. A seleção global continua limitada à versão
antiga aceita; o caminho Remastered permanece fechado. Nada experimental foi
instalado no jogo. Os próximos passos incluem as chamadas reais do motor, câmeras
e históricos por olho, recursos e produção de duas imagens novas por quadro.

## Dispositivo e filas com aquisição própria — 12 de outubro

O registro gráfico moderno agora conserva separadamente o objeto recebido na
chamada e a interface nativa usada pela captura. Uma interface intermediária
pode encaminhar chamadas para o DirectX, mas o seu nome ou a semelhança com uma
lista de comandos não comprova que ela seja um dispositivo gráfico válido.

O fluxo, passo a passo:

1. A verificação nova reconhece especificamente a classe de dispositivo do SDK
   instalado. Confere a tabela de métodos, as funções esperadas, os identificadores
   da classe e da interface de base e a assinatura do corpo inteiro da função de
   consulta. Não lê campos privados do objeto nem guarda uma aprovação permanente.
   As verificações de dispositivo, fila e lista continuam independentes.
2. A aquisição consulta somente o tipo pedido e conserva uma referência própria
   da interface nativa e de sua identidade. Uma referência impede a liberação
   normal daquela interface enquanto está conservada; não congela seus pixels,
   não retém automaticamente o alocador do motor e não resolve todas as chamadas
   concorrentes ou a retirada da DLL.
3. O registro exige uma função explícita de aquisição. O host e o programa de
   teste usam a mesma rotina para dispositivo e fila. Ela aceita DirectX nativo
   e as classes específicas comprovadas do Streamline, sem chamar a API geral
   `slGetNativeInterface`. Outros intermediários permanecem rejeitados nesta
   rota, inclusive quando aparecem dentro de outro intermediário reconhecido.
4. Na criação da cadeia de buffers, conserva o argumento original da fila, a
   interface de dispositivo informada pela cadeia e os endpoints nativos em
   campos separados. Compara as identidades dos dispositivos nativos. A captura
   não troca o argumento enviado à função original nem escolhe outra fila apenas
   porque pertence ao mesmo dispositivo. Não muda a cadeia principal por uma auxiliar.
5. Na recriação com filas explícitas, conserva cada argumento de fila por buffer
   junto de seu endpoint nativo e sua identidade. Quantidade zero continua sem
   leitura dos arrays e fecha a aquisição em caso de sucesso, até uma associação
   explícita válida. Consultas dos filhos do dispositivo usam a mesma política.

`modern_dlss_ownership` faz a verificação por classe e a aquisição com referências;
`modern_graphics_binding.h` conserva o registro e as associações por buffer;
`dxgi_proxy.cpp` fornece essa política ao registro do host. Os dois testes exercitam
as referências e a política utilizada pelo host. Escolhi separar os argumentos
encaminhados dos endpoints porque eles cumprem papéis diferentes: o jogo recebe
seu objeto original, enquanto a captura precisa identificar o DirectX concreto.

A DLL compilou e **113/113 testes passaram em 7,49 segundos**. O programa gráfico
separado passou com **62 cópias reais e pixels conferidos**, usando a nova aquisição
com dispositivos e filas nativos reais. Conferiu também armazenamento dos
argumentos originais, associações por buffer e rejeição sem política de aquisição.

Um teste adicional carregou a DLL instalada do SDK em seu próprio processo e
conferiu o perfil com objetos fabricados contendo somente uma tabela de métodos.
Rejeitou outras classes e uma tabela copiada. Uma ligação real do MinHook alterou
os bytes da função apenas na memória desse processo; a verificação rejeitou a
alteração, voltou a aceitar após restaurar os bytes e rejeitou após descarregar
a DLL. **Esse objeto fabricado não era um dispositivo real do Streamline**, e
nenhuma consulta de dispositivo ou inicialização do SDK foi executada nesse caso.
Os testes de equilíbrio de referências também usam objetos fabricados. As falhas
artificiais de sinal e habilitação dos cenários anteriores continuam simuladas.

**O VR ainda não funciona; nada experimental foi instalado no Witcher.** Não houve
execução de câmera do motor, OpenXR ou Quest. A rota nova ainda precisa ser
verificada com intermediários reais do SDK, com a ordem de instalação do host e
com o ciclo completo de recursos e imagens por olho. A verificação de dispositivo
não autoriza uma classe de recurso, fábrica ou cadeia sem prova própria. Não
resolve concorrência de carregamento, troca de interface ou parada de todas as
chamadas. O bloqueio de versões permanece igual. Primeiro duas imagens novas do
jogo em cada quadro; depois cabeça, mãos, armas, dano e bloqueio físicos no Quest.
DLSS/IntelFG permanecem opcionais; a meta de 60 FPS está cancelada.

## Instalação independente da captura moderna — 12 de outubro

A captura moderna agora pode completar suas quatro ligações com o DirectX quando
for solicitada depois da instalação inicial. Antes, os indicadores gerais de
“já instalado” podiam encerrar essa etapa sem instalar o fechamento da lista de
comandos. Uma lista é o conjunto de instruções enviado à placa de vídeo.

O fluxo, passo a passo:

1. O host confere o modo solicitado, a versão selecionada e o registro gráfico
   atual. Exige interfaces nativas conhecidas e a mesma função concreta de envio
   em todas as filas declaradas. Uma interface intermediária sem prova permanece
   rejeitada; compartilhar um dispositivo não basta.
2. Na primeira tentativa, cria uma lista própria e vazia para localizar os
   métodos concretos. Ela não executa trabalho gráfico e não representa a lista
   ou o alocador do motor do jogo. Tentativas já concluídas não criam outra lista.
3. O helper de instalação reaproveita somente ligações habilitadas com alvo e
   função original exatos. Cria todas as ligações ausentes antes de habilitar
   qualquer uma delas. Um ponteiro preenchido sozinho não vale como instalação.
4. Se a tentativa falhar parcialmente, fecha a admissão e conserva as funções
   originais para encaminhar chamadas já possíveis. Não remove essas ligações,
   não apaga seus ponteiros nem tenta reinstalá-las automaticamente. Isso ainda
   não resolve a parada completa ou a retirada segura da DLL do processo.
5. Os instaladores antigos e o novo compartilham um mecanismo que impede duas
   alterações simultâneas. O host reconfere modo, contrato e registro durante a
   instalação. A apresentação também solicita essa etapa, independentemente dos
   indicadores antigos. Não presume que um resultado do programa de teste
   comprova a instalação dentro do Witcher.

Corrigi também o tratamento da quantidade zero em `ResizeBuffers1`, a chamada
que recria os buffers. Agora não lê os arrays de filas ou máscaras quando o
chamador declarou zero elementos. A descrição anterior do buffer não aumenta o
espaço legível desses arrays. Se a chamada original tiver sucesso nesse caso,
fecha a captura até receber uma associação explícita com quantidade válida;
em falha, conserva os dados anteriores. O resultado original é preservado.
Esta regra **substitui a regra de quantidade zero da etapa anterior**.

`modern_backbuffer_hooks.h` contém o helper usado pelo host e pelo teste;
`dxgi_proxy.cpp` seleciona os alvos e coordena os instaladores;
`modern_graphics_binding.h` conserva associações de buffers e filas;
`modern_eye_source_use_gpu.cpp` exercita essas partes com interfaces reais.
Escolhi reutilizar a mesma rotina de instalação no teste para verificar o
comportamento do código que o host efetivamente chama.

A DLL compilou e **113/113 testes passaram em 10,51 segundos**. O programa separado
conferiu **62 cópias reais com leitura dos pixels**. No caso novo, três ligações
nativas já habilitadas foram reutilizadas; a quarta foi criada e habilitada uma
vez. Repetir o pedido não reinstalou as ligações. O teste também usou a recriação
real dos buffers e recuperou a associação com uma quantidade explícita válida.
A falha parcial de habilitação foi provocada artificialmente. O caso de array
inacessível com quantidade zero usou uma função original simulada; não passou
esse ponteiro inválido ao DirectX. A falha de sinal dos cenários anteriores
continua simulada.

**O VR ainda não funciona e nada experimental foi instalado no jogo.** O teste
não executou os callbacks do host no Witcher, câmeras do motor, OpenXR ou Quest.
O bloqueio de versões continua igual. Os próximos passos são comprovar as
interfaces intermediárias e seus endpoints nativos, fechar o ciclo de vida e
portar câmeras, recursos e históricos por olho. Primeiro duas imagens novas do
jogo em cada quadro; depois cabeça, mãos, armas, dano e bloqueio físicos no Quest.
DLSS e IntelFG permanecem opcionais; a meta de 60 FPS foi cancelada.

## Recriação com filas explícitas por buffer — 12 de outubro

Liguei `ResizeBuffers1` ao registro gráfico moderno. Essa chamada do DirectX
recria os buffers e pode indicar uma fila de comandos diferente para cada um.
O registro precisa conservar a associação fornecida pela chamada, além do
número de época que distingue os buffers anteriores dos novos.

O fluxo, passo a passo:

1. Antes da chamada original, fecha temporariamente novas aquisições da captura
   principal. Obtém referências próprias das filas declaradas por buffer e
   confere interfaces DIRECT, dispositivo e identidades. A igualdade de dispositivo
   sozinha não escolhe uma fila. Uma cadeia auxiliar não altera esse registro.
2. Encaminha a chamada original uma vez, preservando quantidade, formato, flags
   e os ponteiros originais dos dois arrays. Mantém o ajuste de resolução já
   existente no host. Depois usa o resultado e as dimensões realmente retornadas.
3. Em falha, conserva as filas e dimensões anteriores, com uma época nova que
   invalida capturas antigas. Em sucesso, publica a associação explícita válida
   por buffer. A aquisição conserva também a fila correspondente ao índice atual.
4. A regra de quantidade zero desta etapa foi substituída pela correção descrita
   em “Instalação independente da captura moderna”, acima: os arrays não são
   lidos e o sucesso fecha a aquisição até uma associação explícita válida.
   Uma recriação simples posterior
   a um remapeamento explícito fecha a aquisição até uma associação explícita
   nova: aquele caminho não informa suas filas ao registro.
5. A prontidão moderna exige `ResizeBuffers1` realmente habilitado na mesma
   implementação de cadeia. A captura confere os hooks nativos de todas as filas
   declaradas. O campo de fila usado pelo trabalho próprio do XR representa uma
   fila conservada; não afirma que todos os buffers pertencem somente a ela.

`modern_graphics_binding.h` acompanha referências, filas por buffer e épocas;
`dxgi_proxy.cpp` instala e encaminha a recriação e verifica sua prontidão;
`modern_eye_source_use_gpu.cpp` exercita o mesmo helper com DirectX real.
Escolhi conservar os argumentos explícitos porque a fila inicial de criação
não demonstra qual fila foi declarada depois de uma recriação.

A DLL compilou e **113/113 testes passaram em 7,83 segundos**.
O programa separado passou com **29.070 verificações e 62 cópias reais**.
O cenário novo executou `ResizeBuffers1` real, remapeou duas filas do mesmo
dispositivo, escreveu na fila associada ao buffer atual e conferiu os pixels.
Também verificou falha real com buffer retido, quantidade zero com associação
igual, isolamento da cadeia auxiliar e recuperação após fechar uma associação
não comprovada. Foram cinco chamadas originais na cadeia principal e uma auxiliar.
A falha de Signal dos cenários anteriores continua explicitamente simulada.

O cenário novo chama o helper de registro diretamente em seu próprio processo.
Não executou o instalador ou callback de recriação do host dentro do Witcher,
nem comprovou todos os wrappers, aliases, filas de vários dispositivos, mudança
de fila com quantidade zero ou encerramento da DLL. A escrita conferida usa o
buffer corrente; não é uma imagem de olho produzida pelo motor do jogo.

**Nada experimental foi instalado e o VR continua sem versão jogável.** O bloqueio
de versões permanece igual. Ainda faltam os endpoints reais das interfaces
intermediárias, a instalação independente ao mudar de modo, o ciclo de destruição
e o port completo de recursos, câmeras e históricos por olho. Primeiro comprovar
duas imagens novas do jogo por quadro; depois Quest, cabeça, mãos, armas, dano
e bloqueio físicos. A meta de 60 FPS permanece cancelada; DLSS/IntelFG opcionais.

## Captura ligada aos quatro hooks nativos habilitados — 12 de outubro

A captura moderna passa a conferir os pontos de interceptação do DirectX que
usará. Um hook é uma ligação que permite acompanhar uma chamada original.
Ter um ponteiro para ela, sozinho, não demonstra que a ligação foi habilitada.

O fluxo, passo a passo:

1. Os instaladores guardam o alvo e a função original somente depois do resultado
   de criação e habilitação daquele hook. Exigem quatro pontos: transição de
   recursos, fechamento, reinicialização e envio. Um alvo de proxy, uma falha ou
   um conjunto incompleto não dá prontidão à captura moderna.
2. Antes da cópia, confere os métodos concretos da lista e da fila conservada no
   registro gráfico. As interfaces precisam ser nativas, do dispositivo correto
   e usar os mesmos quatro alvos habilitados. A prontidão do consumo das imagens
   também exige esse conjunto. Não herda autoridade de um indicador genérico.
3. No fechamento da lista, captura a época antes de chamar a função original.
   Encaminha a chamada uma vez e conserva seu resultado. Só depois informa esse
   resultado à retenção da origem. Uma chamada de outra lista ou de uma época
   desconhecida não fecha a gravação acompanhada.
4. O envio observado exige fechamento bem-sucedido da gravação correta. Se essa
   prova faltar, a chamada original ainda recebe seu array e as referências
   permanecem conservadas. Fechamento não representa conclusão da GPU: continuam
   valendo a reinicialização comprovada e todos os sinais exatos de cada envio.
5. O caminho interno opaco do ReShade permanece separado. A captura moderna já
   rejeita suas listas imediatas. Não transforme essa exclusão em observação
   inventada, nem desative os hooks de outras listas por uma chamada sem captura.

`modern_backbuffer_hooks.h` confere os quatro registros de instalação;
`modern_backbuffer_source.h` acompanha o fechamento e a retenção dos envios;
`dxgi_proxy.cpp` liga ambos aos instaladores e chamadas originais. A escolha evita
aceitar uma imagem cujo ciclo de envio não tenha esses pontos conhecidos.
Isso ainda não prova todos os caminhos possíveis de wrappers ou destruição.

A DLL compilou e **113/113 testes passaram em 9,26 segundos**.
O programa gráfico separado passou com **27.001 verificações e 59 cópias
reais**, incluindo conferência dos pixels. Instalou os quatro hooks com MinHook,
que é a biblioteca de interceptação usada pelo projeto, nas funções nativas
reais do DirectX. Acompanhou um envio, um fechamento, duas reinicializações e
dez transições no cenário adicional. Preservou o array original de duas listas
e chamou a função original de envio sem manter a trava de metadados.

O teste rejeitou instalação incompleta, métodos misturados, alvo desconhecido,
registro apenas criado sem habilitação e prontidão revogada. Usou também um
fechamento real bem-sucedido para conferir a rejeição de outra época, além de
fechamento de outra lista. O ciclo adicional manteve o buffer enquanto a GPU
estava bloqueada e permitiu sua recriação depois da conclusão comprovada.
A falha de sinal nos casos anteriores continua explicitamente simulada.

O teste instalou MinHook apenas no seu próprio processo, em funções de teste que
usam os mesmos contratos de instalação e retenção. Não instalou os callbacks do
host dentro do Witcher, não desenhou seus olhos e não executou OpenXR ou Quest.
A época e os identificadores de jogo usados pelo teste são fabricados.
Não prova falhas reais de instalação do host, todos os wrappers, a destruição
sem reinicialização, ResizeBuffers1 ou a barreira de encerramento da DLL.

**O VR continua sem versão jogável e nada experimental foi instalado no jogo.**
O bloqueio de versões continua igual. Próximo trabalho: conferir os caminhos
concretos de wrapper e recriação, a retomada de instalação ao mudar de modo e o
ciclo de destruição das listas; depois concluir recursos, câmeras e históricos
por olho e provar duas imagens novas do jogo em cada quadro. Quest, cabeça,
mãos, armas, dano e bloqueio físico ainda dependem dessa prova.
A meta de 60 FPS permanece cancelada; DLSS e IntelFG continuam opcionais.

## Origem conservada até reinicialização e conclusão de todos os envios — 12 de outubro

Liguei a retenção da imagem de origem à gravação e ao envio real dos comandos.
A referência temporária da captura, sozinha, acabava quando a função retornava;
agora o registro próprio continua conservando a origem depois desse retorno.

O fluxo, passo a passo:

1. Antes do primeiro comando de cópia, guarda referências à origem, ao destino,
   à lista de comandos e ao snapshot gráfico. Guarda também os identificadores
   de captura, olho, par, geração e gravação. Não presume posse do alocador ou
   dos objetos internos do motor do jogo.
2. Em cada envio observado, encontra a lista no array que a chamada original
   realmente recebe e confere a gravação. Conserva cada uso com sua fila.
   Encaminha a chamada original uma vez, mesmo se rejeitar a observação.
3. Depois do retorno original, associa o sinal retornado do produtor e seu
   valor. Sinal ausente, desconhecido ou com falha conserva as referências.
   Uma época desconhecida não é convertida em permissão de descarte.
4. Uma gravação pode ser enviada novamente. Por isso a primeira conclusão da
   GPU não libera automaticamente a origem: a lista continua podendo repetir
   aquele uso até uma reinicialização original bem-sucedida encerrar a gravação.
5. Só coleta um registro quando a CPU encerrou o uso, a gravação foi descartada
   por reinicialização comprovada e todos os envios conhecidos concluíram seus
   sinais exatos. Gravações sem envio também precisam desse descarte comprovado.
6. A recriação dos buffers consulta a coleta antes da chamada original.
   Destinos ainda retidos também não são reutilizados ou recriados pela captura.
   A submissão interna opaca do ReShade continua isolada e não ganha essa captura.

`modern_backbuffer_source.h` acompanha a retenção da origem e seus envios;
`dxgi_proxy.cpp` usa esse contrato nas cópias, nos dois caminhos nativos de envio,
na reinicialização e na recriação. Ele é separado da retenção do consumidor XR,
que tem sua própria lista e seu próprio alocador. Escolhi separar os contratos
para não fingir que o mod possui o alocador interno do jogo.

A DLL compilou e **113/113 testes passaram em 8,40 segundos**. O teste gráfico
separado passou com **24.878 verificações e 53 cópias reais**, incluindo leitura
dos pixels. Usou buffers DirectX reais de janelas próprias ocultas, listas com
dois elementos, sinais deliberadamente pendentes, reinicialização real com
sucesso e falha, reinicialização de outra lista e reenvio da gravação. O reenvio
exigiu padrões de pixels novos; os resultados da primeira cópia não serviam
como prova da segunda. Testou leitura em outra fila com espera GPU real.

O diagnóstico corrigiu um cenário do próprio teste que tentava escrever no
buffer por uma fila diferente da associada à cadeia. Os testes finais executam
as escritas na fila válida e a leitura separada com a dependência correta.
A falha de sinal continua explicitamente simulada; criações, cópias, sinais de
conclusão e falhas por buffers ainda retidos foram reais. A recuperação após
sinal falho foi um sinal novo real de esvaziamento usado somente pelo teste;
o host ainda conserva os usos sem prova e não chama essa recuperação.

A execução original e as listas foram preservadas, mas o teste não instalou
MinHook nem executou os callbacks dentro do Witcher. O contrato depende de
observar todos os envios relevantes e não prova a posse dos objetos internos,
do alocador nativo, todos os wrappers ou o encerramento da DLL. Referências
conservam os objetos; a ordem correta dos comandos ainda precisa assegurar que
seus pixels correspondam ao olho certo e ao quadro novo do jogo.

**O VR ainda não é jogável e nada experimental foi instalado. O bloqueio de
versão continua igual.** Ainda faltam a cobertura real dos callbacks e de
recriação, o port completo de câmeras, recursos e históricos por olho, o ciclo
de produção e cancelamento do par, a barreira de encerramento e a prova de duas
imagens novas do jogo por quadro. Depois vêm Quest, cabeça, mãos, armas, dano,
bloqueio e parry físicos. A meta de 60 FPS permanece cancelada.

## Referências gráficas próprias e invalidação após recriação — 12 de outubro

Liguei a captura moderna a um registro que possui referências ao dispositivo,
à fila gráfica e à cadeia de apresentação. A cadeia de apresentação é o conjunto
de buffers que o DirectX entrega para mostrar a imagem. A captura passa a obter
um snapshot desse registro: um conjunto de referências conservadas, acompanhado
de um número de época. Esse número muda quando os buffers são recriados.

O fluxo, passo a passo:

1. Depois da criação original bem-sucedida, recebe a fila que foi entregue à
   criação e a cadeia que ela retornou. Confere o tipo da fila, as interfaces,
   o dispositivo correspondente e as dimensões reais. Não deduz uma fila a
   partir de outra que apenas usa o mesmo dispositivo.
2. Guarda as referências em um registro protegido. A aquisição copia a posse
   compartilhada do snapshot dentro da trava, sem chamadas COM nessa trava.
   As chamadas COM são operações das interfaces gráficas do Windows.
3. Obtém o buffer atual a partir da cadeia conservada e confere seu dispositivo.
   A gravação guarda a época que originou a captura. Publicação e consumo dos
   dois olhos exigem essa mesma época atual; uma época antiga não ganha validade
   porque as texturas ainda existem.
4. Durante uma recriação observada, fecha a aquisição, encaminha a chamada
   original uma vez e preserva seu resultado. Depois cria uma nova época.
   Uma falha conserva as dimensões anteriores; um sucesso consulta as dimensões
   reais. O callback só atualiza as dimensões depois do sucesso original.
5. Uma cadeia auxiliar não substitui silenciosamente a selecionada. Seus callbacks
   de recriação continuam encaminhando a chamada original sem trocar a autoridade
   da cadeia principal. Substituir a cadeia principal inteira ainda exige esvaziar
   e encerrar seus usos corretamente.
6. O consumo conserva seu snapshot junto das duas texturas enquanto estiver em
   uso. Uma revogação impede novas aquisições e não destrói essas referências já
   conservadas. Essa revogação, sozinha, não encerra todas as chamadas do mod.

`modern_graphics_binding.h` guarda e confere essas referências;
`modern_eye_pair.h` exige a época atual nos registros dos dois olhos;
`dxgi_proxy.cpp` liga esses componentes aos callbacks reais de criação,
captura, publicação, consumo e recriação. O helper de sinais também obtém o
dispositivo diretamente da fila válida recebida pelo callback. Escolhi conservar
os objetos e validar a época separadamente: o objeto pode continuar vivo e,
ainda assim, sua captura já pertencer a uma configuração antiga.

A DLL compilou e **113/113 testes passaram em 12,58 segundos**. O teste de pares
conferiu **484 regras CPU**, com dados fabricados para ambas as versões. O teste
gráfico separado passou com **14.556 verificações e 24 cópias reais de textura**.
Também criou cadeias DirectX reais em seu próprio processo: uma janela oculta,
uma falha real de recriação enquanto o buffer estava retido, nove recriações
bem-sucedidas da cadeia principal e uma recriação de cadeia auxiliar. Três threads
obtiveram snapshots durante as trocas. O teste não abriu o Witcher ou o Quest.

As falhas de sinal e de alocação dos casos anteriores continuam explicitamente
simuladas; a falha de recriação com buffer retido foi um resultado real do DirectX.
As referências conservam objetos, não congelam pixels. A nova aquisição mantém
buffer e snapshot durante a gravação, mas ainda falta conservá-los até o sinal
real da submissão do produtor terminar. Não comprova todos os wrappers, trocas
de cadeia principal, recursos internos do motor, MinHook, OpenXR ou encerramento
da DLL. A revogação na entrada do encerramento apenas fecha a aquisição; as demais
operações de encerramento e a barreira de todas as chamadas continuam pendentes.

**Ainda não há VR jogável, o bloqueio de versão continua igual e nada experimental
foi instalado no jogo.** O próximo passo é ligar a retenção da origem à gravação,
ao envio e ao sinal reais do produtor, com descarte apenas após reinicialização
original comprovada ou conclusão da GPU. Depois permanecem o port completo do
motor, duas imagens novas do jogo por quadro e a integração física de cabeça,
mãos, armas, dano e bloqueio. A meta de 60 FPS continua cancelada.

## Troca conjunta de texturas preservando o conjunto anterior — 12 de outubro

Corrigi as duas rotinas que recriam as imagens próprias quando seu tamanho ou
formato muda. Antes, a recriação podia liberar o conjunto antigo e depois falhar
no meio da criação do novo. Agora, conserva o antigo até concluir a troca inteira.

O fluxo, passo a passo:

1. Reúne os endereços que possuem as quatro texturas de apresentação ou as
   seis texturas de captura. Confere que não são endereços repetidos.
2. Na rota moderna, rejeita a troca enquanto há uma gravação pendente,
   referências conservadas pelo consumidor ou um sinal de uso ainda não
   concluído. Consulta o sinal real da GPU; uma espera agendada não basta.
3. Cria todas as novas texturas em referências temporárias. Se qualquer criação
   falhar, libera somente essas novas referências; conserva as imagens antigas,
   seus tamanhos e os registros de captura. Não publica um conjunto pela metade.
4. Antes de substituir, confere novamente os endereços e o uso das imagens.
   Depois coloca todas as novas referências no conjunto e libera as antigas.
   Só após o sucesso o mod limpa os registros antigos e atualiza o tamanho.
5. O conjunto de captura só é considerado completo quando as seis texturas
   existem. Uma textura isolada não comprova que o conjunto inteiro foi criado.

`modern_eye_resource_set.h` faz essa troca; `dxgi_proxy.cpp` usa o componente
nas duas rotinas reais de recriação e fornece os sinais e a criação DirectX.
O teste gráfico usa o mesmo componente. Escolhi preparar e conferir o conjunto
inteiro antes de trocar para conservar uma situação coerente se algo falhar.
A troca adiada não apresenta o conjunto antigo como duas imagens novas.

O teste físico passou com **24 cópias reais e 14.530 verificações**, incluindo
leitura dos pixels. Conferiu conjuntos de quatro e seis recursos, trabalho
real da GPU deliberadamente pendente, referências ainda usadas pela CPU,
gravação pendente, sinais ausentes ou desconhecidos, endereços alterados durante
a criação, recursos repetidos e falha em cada posição de criação do conjunto.
Depois das trocas rejeitadas, copiou novamente os pixels das texturas antigas
para conferir que continuavam utilizáveis. Ao concluir a troca, todas as novas
texturas tinham 32 por 32 pixels e eram distintas; as antigas tinham 16 por 16.

As falhas de criação e de sinal foram resultados explicitamente simulados.
As criações que tiveram sucesso, interfaces, filas, cópias, demais sinais e
leitura dos pixels foram reais. Não esgotou a memória de vídeo nem induziu
remoção do dispositivo. O teste gráfico é separado dos **113 testes da DLL**,
que passaram em **10,89 segundos**. Não carregou o jogo, OpenXR ou o Quest.

A proteção aplica-se a essas duas rotinas próprias. Não comprova todos os
caminhos de troca e destruição do jogo, proteção das variáveis globais antes
de obter uma referência, concorrência dos interceptadores nem encerramento
da DLL. O código continua dependendo da trava de captura do chamador, que
impede alterações simultâneas nesses conjuntos; ela não encerra todas as
chamadas do jogo. A política de sinais da rota antiga foi conservada.

**O VR ainda não é jogável. Nada experimental foi instalado no jogo e o bloqueio
geral de versão permanece igual.** Ainda faltam a aquisição protegida dos
recursos do jogo, o ciclo completo de produção e cancelamento do par, recursos
e históricos do motor por olho e a comprovação das duas imagens reais do jogo
por quadro. Depois vêm Quest, cabeça, mãos, armas, dano e bloqueio físicos.
A meta de 60 FPS permanece cancelada; DLSS e geração de quadros são opcionais.

## Retenção de texturas testada com DirectX real — 12 de outubro

Separei o registro de uso das texturas em um componente usado pelo próprio
mod e pelo teste gráfico. Nesta etapa, o teste criou recursos e executou cópias
reais na placa de vídeo do PC. Não carregou o jogo, o headset ou OpenXR, que é
a interface de comunicação com o sistema VR. As imagens do teste são padrões
de laboratório; não são imagens produzidas pelo Witcher.

O fluxo, passo a passo:

1. O mod entrega ao componente as referências das duas texturas, dos sinais,
   das filas de trabalho, da lista de comandos, do alocador e do destino XR.
   Referências mantêm essas interfaces vivas; não garantem que seus pixels
   permaneçam iguais nem protegem todos os objetos internos do jogo.
2. Antes de enviar, confere que o registro está em gravação e corresponde
   à fila, à lista e à quantidade realmente entregues para execução. Uma
   rejeição fecha esse envio; não chama a execução original mesmo assim.
3. Depois do envio original, recebe o resultado real do sinal e seu número.
   O uso enviado só fica disponível para coleta quando esse sinal termina e
   a CPU já encerrou seu uso. Falha de sinal conserva as referências.
4. Uma reinicialização original com sucesso pode descartar apenas a gravação
   abandonada daquela lista. Não libera um uso já enviado. Falha de fechamento
   ou reinicialização não ganha uma permissão de descarte por suposição.
5. O teste cria duas texturas de 16 por 16 pixels com padrões diferentes,
   bloqueia deliberadamente o trabalho da GPU e retira as referências do
   chamador. Cria novas texturas de 32 por 32 enquanto as antigas continuam
   pendentes. Confere que o componente ainda conserva as antigas.
6. Libera o bloqueio do teste, espera o sinal correto e lê os pixels de volta
   para a CPU. Confere todos os bytes úteis de ambos os padrões e só então
   coleta o uso. Repete com produtor e consumidor na mesma fila e em filas
   diferentes, preservando a espera real entre essas filas.

`modern_eye_source_use.h` contém o registro e a coleta usados pelo mod;
`dxgi_proxy.cpp` fornece seus dados e encaminha as chamadas reais;
`modern_eye_source_use_gpu.cpp` usa o mesmo componente em um processo separado.
Escolhi compartilhar a implementação para que o teste exercite as operações
que o mod usa, sem depender de uma cópia paralela dessas regras.

O teste físico passou com **12 cópias reais de textura e 6.191 verificações**,
incluindo conferência dos pixels. Testou retenção enquanto a GPU estava
pendente, troca de tamanho dos recursos do chamador, conclusão com o sinal
correto, argumentos de envio alterados e reinicialização e fechamento reais
com sucesso e falha. O erro de sinal foi simulado por um resultado explícito;
as cópias, interfaces, sinais posteriores e leitura dos pixels foram reais.
Não induziu remoção do dispositivo. A repetição de sinal usada para encerrar
essa hipótese é própria do teste; o mod conserva seu bloqueio após essa falha.

A DLL compilou e **113/113 testes passaram em 11,02 segundos**. O teste
gráfico é um executável separado e não faz parte desses 113 testes. Seus
resultados não comprovam a instalação dos interceptadores no Witcher, a
aquisição segura de objetos internos, todos os caminhos de troca de tamanho
ou encerramento, concorrência no jogo, imagens estéreo do jogo, nem o Quest.

**O VR ainda não é jogável e nada experimental foi instalado no jogo.** O
bloqueio geral continua igual. Ainda faltam recursos e históricos do motor
por olho, a ordem completa de desenho e envio, proteção contra chamadas em
execução durante a retirada da DLL e a comprovação das duas imagens reais do
jogo. O componente não resolve sozinho esse encerramento: usos sem prova de
conclusão conservam suas referências. Depois das imagens reais no PC vêm
Quest, cabeça, mãos, armas, dano e bloqueio físicos. A meta de 60 FPS permanece
cancelada.

## Consumidor dos dois olhos novos e retenção das texturas — 11 de outubro

Liguei a seleção dos dois olhos aos registros reais de captura e envio dos
comandos. Esta etapa usa NoAA, o modo sem filtro de suavização da imagem.
Os registros ajudam a impedir misturas; ainda não demonstram que o jogo tenha
desenhado corretamente os dois olhos, pois esse caminho não foi executado nele.

O fluxo, passo a passo:

1. Cada cópia guarda sua versão, geração, par, olho físico, textura própria,
   número da cópia e gravação de comandos. Uma geração identifica o conjunto
   atual de recursos; a gravação muda quando a lista de comandos é reiniciada.
   Não inventa esse número quando a reinicialização ainda não foi observada.
2. Confere esses dados na lista realmente entregue à placa de vídeo. Depois
   da chamada original de envio, associa o sinal do produtor e a espera da
   fila consumidora. Agendar uma espera não significa que o trabalho terminou.
   Um registro rejeitado permanece reservado até uma reinicialização observada
   poder descartar sua gravação; não desaparece só porque a validação falhou.
3. Aceita o conjunto apenas quando ambos pertencem à mesma versão, geração,
   responsável pelo desenho e par. Exige olhos físicos distintos, duas texturas,
   duas cópias novas e gravações compatíveis com o envio observado. Uma mesma
   lista pode conter as duas cópias; duas listas, sozinhas, não provam dois olhos.
4. Guarda as texturas e suas posições e lentes como um único conjunto. Mantém
   referências às interfaces dos produtores, sinais, fila consumidora, alocador
   de comandos e destino XR. Uma referência conserva o objeto; não congela
   automaticamente seus pixels nem comprova toda a vida útil do motor do jogo.
5. Reserva cada conjunto para uma única tentativa de apresentação. Uma falha
   não transforma esse conjunto em uma imagem nova por repetição. Nessa rota
   moderna de jogo, não usa olhos alternados, uma cópia mono ou um par antigo
   como substitutos. Sem um conjunto válido, o ciclo iniciado do headset pode
   terminar sem imagem de jogo, preservando a leitura da cabeça. Um ciclo já
   preparado para o par continua esperando os dois olhos; menus têm rota própria.
6. O desenho usa as texturas e as posições guardadas, incluindo o tamanho da
   textura retida. Não precisa alterar o estado do buffer de apresentação do
   jogo nessa cópia. Confere novamente a admissão antes de entregar a imagem.
7. Conserva referências enquanto a CPU ainda usa o conjunto e enquanto sua
   submissão pode estar em execução. Só coleta usos enviados após o sinal exato
   terminar; um erro de sinal ou remoção do dispositivo não libera os objetos.
   Falha de sinal fecha o consumidor. Uma gravação própria abandonada só perde
   suas referências depois de sua reinicialização original ter tido sucesso.
8. Impede reutilizar ou recriar o conjunto de texturas enquanto existirem
   gravações pendentes, sinais não concluídos ou usos retidos pelo consumidor.
   Isso é uma proteção desse caminho; não resolve todos os caminhos de troca
   de tamanho, encerramento, chamadas simultâneas ou retirada da DLL.

`modern_eye_pair.h` reúne a comparação dos registros, a tentativa única e as
regras de coleta; `dxgi_proxy.cpp` fornece as observações reais e mantém as
referências. Escolhi guardar o conjunto inteiro para que uma atualização do
cache durante a apresentação não troque apenas um olho ou sua posição.

A DLL compilou e **113/113 testes passaram em 9,49 segundos**. O novo teste
fez **456 verificações na CPU**: ambas as versões, dados misturados, olhos e
texturas repetidos, reinicialização de listas, cópias antigas, alternativas de
apresentação, tentativas concorrentes, falhas, troca de geração e condições
de coleta. Usou dados fabricados. Não executou as chamadas do jogo, a instalação
dos interceptadores, DirectX na GPU, OpenXR, Quest ou as operações reais de
retenção das interfaces. Compilar essas operações não valida sua execução.

**O VR continua sem uma versão jogável e nada experimental foi instalado.**
O bloqueio geral permanece igual. Ainda é necessário verificar o caminho real
de desenho, gravação e envio, a vida útil dos objetos de origem, todos os
reinícios e destruidores, os históricos e efeitos por olho e a retirada segura
da DLL. A proteção deste consumidor não é uma barreira para todas as chamadas.
Primeiro comprovar duas imagens reais no PC; depois Quest, cabeça, mãos, armas,
dano e bloqueio físicos. A meta de 60 FPS continua cancelada.

## Preparação e envio do quadro ligados à mesma versão — 11 de outubro

Adaptei a preparação dos quadros e dos efeitos à atualização atual. Um quadro
é o conjunto de dados que o jogo usa para desenhar uma imagem. A mudança
acompanha a preparação normal antes de preparar e enviar a cópia extra;
essa parte, isoladamente, ainda não produz imagens no headset.

O fluxo, passo a passo:

1. Escolhe a lista da versão já identificada e exige que a instalação da fila
   pertença à mesma versão. A lista reúne cinco funções, os pontos de onde
   devem ser chamadas e os dados usados por elas. Versões ausentes, copiadas,
   desconhecidas ou misturadas não ganham uma lista por padrão.
2. Confere o início completo das cinco funções, cria as cinco ligações e
   depois ativa as cinco. Faz uma única tentativa. Só declara prontidão após
   todas passarem e a seleção continuar igual. Uma falha desativa as ligações
   já ativadas e conserva os caminhos para encaminhar chamadas originais.
3. Ao entrar na preparação normal, guarda a versão e a permissão daquela
   entrada. Uma entrada rejeitada esconde temporariamente o contexto anterior;
   terminar a instalação durante a chamada não a transforma em entrada aceita.
   Ao sair, restaura o contexto anterior, inclusive numa exceção comum de C++,
   que é um erro tratado pela própria linguagem. Isso não testa falhas de memória.
4. Confere os dados reais do responsável pelo desenho, da fila e dos efeitos.
   Se houver uma chamada extra do mundo, só aceita o alvo conhecido da mesma
   versão que comprovadamente não faz trabalho. Conserva a chamada original
   do jogo. Não repete atualizações de objetos nem tenta manter objetos vivos
   apenas porque duas leituras de seus endereços coincidem.
5. Acompanha a preparação original do quadro principal, o avanço do tempo
   dos efeitos e a aplicação desses efeitos. Encaminha os argumentos originais
   e confere novamente versão, dados e ordem depois da chamada. O quadro
   principal continua sob controle do jogo; apenas a cópia extra fica pendente.
6. No ponto normal de construção do comando, prepara a cópia extra e aplica
   os efeitos observados. Não avança seu tempo novamente. Reserva o comando
   na mesma fila capturada, usa a construção e o envio da mesma versão e
   preserva as referências originais, que são os registros de uso do objeto.
7. Uma mudança de versão, dados, ordem ou rota descarta a cópia pendente sem
   consumir o quadro principal. Retira a cópia do estado pendente antes de
   chamar as funções originais, evitando que uma chamada feita por dentro
   tente enviar ou liberar essa mesma cópia uma segunda vez.

`engine_frame_preparation.h` reúne as listas, a instalação, os contextos e a
cópia pendente; `engine_frame_submission.h` limita o envio à lista conhecida;
`dxgi_proxy.cpp` liga essas regras às chamadas do jogo. Escolhi observar a
preparação natural para conservar sua ordem e o único avanço do tempo dos
efeitos. Não deduzi que argumentos temporários pertencem a um objeto específico.

A DLL compilou e **112/112 testes passaram em 9,51 segundos**. Os testes de
preparação fizeram **1.648 verificações na CPU**, incluindo 1.288 novos casos
de versões, contextos e instalação; os de envio fizeram **114 verificações**,
incluindo 16 novos casos de seleção. As rotinas de ordem e uso dos objetos
foram executadas para ambas as versões. As falhas de cada criação e ativação,
mudanças durante a instalação, misturas, chamadas internas, saída por erro
e conclusão imediata ou adiada usam dados fabricados e funções substitutas.
Não executaram funções do jogo, instalação real dos interceptadores, falhas
reais de memória, GPU, Quest, mãos ou combate. As análises novas são privadas.

**O VR ainda não funciona e nada experimental foi instalado.** O bloqueio geral
de compatibilidade continua igual. Preparação e envio reconhecidos não provam
que existam duas imagens novas do mesmo par, que os efeitos e seus históricos
estejam separados corretamente, nem que os objetos permaneçam vivos durante
execução, troca de tamanho ou retirada da DLL. Esses pontos continuam pendentes.
Primeiro imagens reais no PC; depois Quest, cabeça, mãos, armas, dano e bloqueio
físicos. A meta de 60 FPS permanece cancelada.

## Fila de quadros ligada à instalação da mesma versão — 11 de outubro

Adaptei os pontos que acompanham a criação, a parada e a destruição da fila
para a atualização atual. A fila recebe os pedidos de desenho do jogo. Um
observador acompanha esses acontecimentos e encaminha a chamada original;
não cria sozinho uma imagem de olho nem mantém os objetos do jogo vivos.

O fluxo, passo a passo:

1. Escolhe explicitamente a lista da versão já identificada. Essa lista reúne
   a fila, seus pontos de entrada, os chamadores de criação e as assinaturas
   esperadas. Assinatura significa os primeiros bytes da função; todas terminam
   no fim de instruções completas e foram conferidas em cada versão.
2. Confere os três pontos, cria as três ligações e depois ativa as três. Só
   anuncia prontidão quando todas passam e a seleção continua igual. Faz uma
   única tentativa. Uma falha conserva os caminhos para chamar as funções
   originais e desativa as entradas já ativadas; não remove esses caminhos.
3. Cada criação registra endereço, número de criação e versão da instalação.
   Um endereço reutilizado perde o registro anterior, mesmo se a nova chamada
   for rejeitada. Uma chamada que entrou antes da prontidão não ganha permissão
   só porque a instalação terminou durante a execução da função original.
4. Depois da função original, confere chamador, modo, resultado e dados da
   mesma versão. Lê o responsável informado pelo construtor antes de o jogo
   guardar a fila nesse responsável. Conserva a gravação e o resultado originais.
5. Ao concluir o registro, exige o mesmo número de criação e a mesma versão
   da identidade lida. Dados de outra atualização, mesmo com endereços iguais,
   não completam o registro. A rejeição de uma criação antiga também não cancela
   uma criação nova. Parada e destruição retiram o registro antes da chamada
   original, conservando argumentos, resultado e uma execução dessa chamada.
6. As leituras da fila em uso e a reserva de comandos agora recebem uma lista
   explícita da mesma versão. Conferem novamente a seleção e a instalação;
   conservam o responsável, o tamanho pedido e o resultado da reserva original.
   Comparar duas leituras não mantém a memória viva durante uma troca ou parada.
7. A preparação de quadros e efeitos que ainda usa regras da versão anterior
   rejeita a atualização atual antes de ler seus dados. Uma fila reconhecida não
   libera essa preparação nem o bloqueio geral do jogo. As versões ausentes,
   desconhecidas, copiadas ou misturadas continuam sem admissão.

`modern_queue_lifecycle.h` reúne as listas, a instalação e os registros de
criação; `modern_command_queue.h` identifica a fila e limita a reserva;
`dxgi_proxy.cpp` liga as regras às chamadas do jogo. Escolhi guardar a versão
no registro porque o mesmo endereço e um sinal geral de “pronto” não distinguem
uma criação nova nem identificam as funções realmente instaladas.

A DLL compilou e **112/112 testes passaram em 10,00 segundos**. O teste da
fila fez **510 verificações na CPU**, 320 a mais nesta etapa. Incluem ambas
as versões, limites das leituras, misturas, endereços reutilizados, parada
durante criação, falhas em cada criação e ativação e troca da seleção durante
as operações simuladas. Usam dados fabricados e substitutos das operações de
instalação; não executaram MinHook no jogo, falhas reais de memória, GPU,
Quest ou combate físico. As análises novas do executável permanecem privadas.

**O VR ainda não funciona e nada experimental foi instalado.** Remastered e
reentrada continuam bloqueados. Ainda faltam a preparação atual, seus efeitos,
históricos, recursos e a apresentação de duas imagens novas do mesmo par.
Esses observadores não comprovam todos os caminhos de destruição, a proteção
contra chamadas em execução durante a retirada da DLL, nem a conclusão do
trabalho da placa de vídeo. Primeiro imagens reais no PC; depois Quest,
cabeça, mãos, armas, dano e bloqueio físicos.

## Preparação dos dois olhos ligada à versão atual — 11 de outubro

Adaptei os dados de entrada da criação de quadros e a preparação de posição
e lente para a atualização 1048522. Dados de entrada são o bloco que o jogo
recebe antes de construir um quadro. Preparar esses dados não desenha a imagem.

O fluxo, passo a passo:

1. Escolhe explicitamente a versão já identificada. Uma seleção ausente,
   desconhecida ou copiada não recebe uma versão por padrão. A versão antiga
   conserva sua própria rota; as duas versões modernas têm endereços próprios.
2. Confere que existem o responsável pelo desenho e os dados da cena, e que
   largura e altura são legíveis e maiores que zero. Nas versões modernas, o
   segundo argumento de configurações pode estar vazio, como na chamada do jogo.
3. Copia somente um bloco de bytes emprestado durante a chamada original.
   Exige o bloco moderno completo, alinhamento e uma quantidade limitada de
   elementos internos. A regra de cópia identifica sua versão; tamanho igual
   não permite usar a regra de outra versão. Os ponteiros continuam emprestados:
   a cópia não cria nem passa a possuir os objetos do jogo.
4. Congela as duas posições, rotações e lentes fornecidas pelo headset junto
   com a centralização. Escolhe os campos da mesma versão para ler e escrever.
   Calcula os dois olhos antes de entregar qualquer um; preserva a distância
   física entre os olhos e a relação entre as duas câmeras internas de cada
   quadro. Essas câmeras internas não são os dois olhos do headset.
5. Altera só os campos previstos em duas cópias privadas. A lente é aplicada
   apenas à câmera principal, substituindo o centro existente. Conserva a
   pequena variação temporal da imagem, suas dimensões inteiras, profundidade,
   matrizes, histórico e referências. Uma leitura de posição inválida também
   deixa o resultado anterior intacto.
6. Entrega as duas cópias com a versão e o instante da leitura do headset.
   Se qualquer lado falhar, não entrega metade do par nem muda sua identificação
   anterior. Antes de usar o par, confere ambos os blocos, versão e instante;
   a integração também reconfere a geração de imagens e a seleção vigente.
7. Reconhecer os dados da versão atual não permite instalar a fábrica ignorando
   a fila e a preparação ainda não adaptadas. Os bloqueios dessas partes e o
   bloqueio geral permanecem fechados. Essas conferências não impedem sozinhas
   a destruição de objetos ou a retirada da DLL durante chamadas em execução.

`engine_scene_factory.h` escolhe a versão e confere os dados de entrada;
`engine_scene_descriptor.h` limita a cópia; `modern_camera_pose.h` prepara o
par e sua identificação; `modern_camera_projection.h` calcula a lente;
`dxgi_proxy.cpp` liga essas regras ao caminho existente. Escolhi passar a
versão explicitamente porque campos iguais não comprovam que uma chamada
pertence à mesma atualização.

A DLL compilou e **112/112 testes passaram em 8,53 segundos**. Os três testes
alterados fizeram **39, 68 e 914 verificações na CPU**, respectivamente.
Incluem ambas as versões, rejeição de seleções misturadas e copiadas, campos
inválidos, preservação de bytes, lentes diferentes e comparação com contas
independentes de projeção. Usam dados fabricados; não executaram as funções
do jogo, instalação real das ligações, falhas reais de memória, GPU ou Quest.
As análises novas do executável continuam privadas.

**O VR ainda não funciona; Remastered e a reentrada continuam bloqueados.
Nada experimental foi instalado.** Faltam a preparação nativa completa,
recursos, históricos, efeitos, vida útil e apresentação de duas imagens novas
do mesmo par. Depois vêm o teste de imagem e cabeça no Quest e a integração
de mãos, armas, dano e bloqueio físicos. DLSS continua complementar.

## Cópias de câmera vinculadas à instalação da mesma versão — 11 de outubro

Adaptei o conjunto de três observadores de cópia e reconstrução para a
atualização atual. Um observador acompanha a chamada do jogo e encaminha
seus argumentos e resultado; ele não cria uma imagem nem controla uma mão.
A instalação usa a lista de funções e assinaturas da versão escolhida.
Assinatura aqui significa os primeiros bytes esperados da função.

O fluxo, passo a passo:

1. Escolhe uma lista conhecida e confere os três pontos antes de criar as
   ligações. As assinaturas terminam no fim de instruções completas e foram
   conferidas separadamente nos arquivos das duas versões.
2. Cria as três ligações, depois ativa as três. Só anuncia prontidão quando
   todas passam e a versão escolhida continua igual. Uma falha fecha a
   admissão; conserva os caminhos de encaminhamento e impede nova tentativa.
3. Cada chamada confere a versão instalada e a selecionada antes de observar
   os dados e depois de executar a função original. Outra versão, uma lista
   copiada, uma lista desconhecida ou prontidão perdida não completam a cópia.
4. Uma chamada rejeitada recebe um contexto vazio durante sua execução.
   Contexto é o registro temporário de quem iniciou aquela sequência. Ao
   retornar, restaura o registro anterior, inclusive em chamadas encaixadas
   ou quando uma exceção comum de C++ interrompe o caminho.
5. A conclusão da criação de um quadro exige a sequência de cópias completa
   e a instalação correspondente ainda pronta. A atualização atual e versões
   desconhecidas deixam de receber conclusão automática por serem diferentes
   da versão anterior. As duas câmeras internas continuam sendo registros de
   um quadro, não os dois olhos do headset.
6. As ligações modernas seguem o encaminhamento moderno mesmo se a seleção
   mudar durante uma chamada. Preservam a função original uma vez e não
   entram nas correções antigas de posição, lente ou distância de desenho.

`engine_camera_copy_context.h` reúne as listas, a instalação em conjunto e
os registros temporários; `dxgi_proxy.cpp` liga essas regras às chamadas do
jogo. Separei o acompanhamento moderno da função antiga com proteção de
memória do Windows para permitir a compilação sem mudar essa proteção.
Escolhi conferir a instalação junto com o contexto porque um sinal geral de
“pronto” não identifica a versão das funções realmente ligadas.

A DLL compilou e **112/112 testes passaram em 8,20 segundos**. O teste de
câmeras fez **16.472 verificações na CPU**, 600 a mais nesta etapa. Incluem
falhas em cada ponto de criação e ativação, troca de contrato, preservação
dos encaminhamentos, versões misturadas e restauração do contexto. Os testes
substituem as operações de instalação por funções simuladas; não executaram
MinHook no jogo, falhas reais de memória, GPU, Quest ou combate físico.
As evidências novas do executável permanecem privadas.

**O bloqueio geral de Remastered e da reentrada continua fechado. O VR ainda
não funciona e nada experimental foi instalado.** Esse acompanhamento não
retém os objetos do jogo nem protege sozinho a retirada da DLL enquanto
outras chamadas estejam em execução. Ainda faltam a criação dos quadros e
seus dados de entrada, históricos, efeitos, vida útil dos recursos e a
apresentação do mesmo par de imagens novas. Primeiro imagens reais no PC;
depois Quest, cabeça, mãos, armas, dano e bloqueio físicos.

## Campos e controle da câmera separados por versão — 11 de outubro

Adaptei a lista de campos da câmera, as rotas de atualização temporal e os
nove pontos que informam o controle da câmera para a atualização 1048522.
Um perfil é uma lista de posições e regras conferida para uma versão do jogo.
A versão anterior conserva sua própria lista; cópias e listas desconhecidas
não recebem autorização para escrever dados ou selecionar chamadas.

O fluxo, passo a passo:

1. Escolhe explicitamente o perfil da versão já selecionada. A posição do
   jogador, a rotação, as dimensões inteiras da imagem e os campos históricos
   são tratados separadamente. Não calcula todos os campos deslocando os
   números da versão antiga pela mesma quantidade.
2. As funções de escrita verificam tamanho e valores antes de alterar os
   poucos campos previstos. Conservam todos os bytes vizinhos, inclusive
   matrizes, referências e histórico. As duas câmeras internas pertencem
   a um quadro do jogo; elas não são os dois olhos do headset.
3. A atualização temporal distingue o desenho normal, a captura ampliada,
   a restauração e a troca para desenho em duas dimensões. Cada versão tem
   suas próprias rotas. A função moderna encaminha os cinco argumentos
   originais uma vez e retorna antes das correções antigas, evitando somar
   novamente o centro da lente à pequena variação temporal da imagem.
4. Os nove pontos de controle incluem cabeça, posição de osso, cenas,
   controle manual, câmera principal, vídeo de carregamento e direção.
   Foram vinculados ao nome registrado e à classe correspondente no arquivo
   do jogo. A identificação por classe impede escolher outra função que
   tenha os mesmos primeiros bytes. A leitura continua sem alterar esses
   estados; ainda não existe acompanhamento de mãos ou armas no jogo.
5. O ponto de atualização temporal verifica os primeiros bytes da função
   esperada antes de instalar sua ligação. O controle manual tem uma
   assinatura própria por versão. Assinatura significa a sequência de bytes
   esperada naquele ponto, não uma prova de funcionamento em execução.
6. A cópia e a reconstrução modernas seguem a rota de encaminhamento antes
   de qualquer correção antiga. O instalador do conjunto de três observadores
   da atualização atual ainda precisa de adaptação; a seleção moderna não
   pode cair no instalador individual antigo para contornar esse limite.

`engine_camera_layout.h` reúne os campos e as rotas de atualização;
`engine_camera_authority.h`, os pontos e estados de controle;
`dxgi_proxy.cpp` aplica a seleção e encaminha as chamadas originais. Escolhi
listas separadas porque campos iguais não significam endereços de funções
iguais, nem a mesma classe em execução.

A DLL compilou e **112/112 testes passaram em 11,20 segundos**. O teste de
campos fez **15.872 verificações na CPU**, incluindo limites de tamanho,
valores inválidos, preservação dos bytes vizinhos, as duas versões, os dois
registros internos, rotas e assinaturas misturadas. São dados fabricados:
não executaram as funções do jogo, a instalação real das ligações, a GPU,
o Quest ou combate físico. As análises novas do executável continuam privadas.

**O bloqueio geral de Remastered e da reentrada continua fechado. O VR ainda
não funciona e nada experimental foi instalado.** Ainda faltam o instalador
atual das cópias, a fábrica e seus dados de entrada, os efeitos e históricos,
a vida útil dos recursos e a apresentação do mesmo par de imagens novas.
Primeiro serão verificadas imagens reais no PC; depois, cabeça no Quest,
mãos, armas, dano e bloqueio físicos. DLSS e IntelFG continuam opcionais;
a meta de 60 FPS permanece cancelada.

## Visibilidade separada por versão — 11 de outubro

Adaptei a escolha das câmeras de vegetação e dos objetos visíveis para a
atualização 1048522. O perfil da versão anterior permanece separado.
Um perfil é uma lista de pontos e regras conferidos para uma versão do jogo.

O fluxo, passo a passo:

1. Escolhe um perfil conhecido e guarda essa escolha junto do quadro e do olho.
   Uma lista copiada, desconhecida ou pertencente à outra versão não ganha
   autorização para mudar a câmera.
2. Na vegetação, reconhece separadamente as atualizações antecipadas e tardias
   de cada versão. Usa a posição, a visão, a projeção e o alcance próximo da
   câmera principal daquele olho. Conserva o alcance distante escolhido pelo
   jogo, os demais argumentos e a resposta original.
3. Na seleção dos objetos do mundo, confere o quadro recebido pela própria
   tarefa. Na cópia privada específica, oferece o endereço da câmera principal
   ao jogo; o jogo continua fazendo a cópia, o ajuste próximo e a reconstrução.
   O mod conserva o destino e o retorno original, sem construir uma câmera por
   simples cópia de memória nem reaplicar o movimento da cabeça.
4. O conjunto de cinco pontos de ligação usa os endereços, os primeiros bytes
   das funções e o tipo da tarefa da mesma versão. Só anuncia disponibilidade
   depois de ligar todos. Uma falha parcial fecha a admissão e conserva as
   chamadas originais; isso ainda não resolve o desligamento seguro do mod.
5. Confere a versão instalada antes e depois de preparar a troca. Chamadas
   internas rejeitadas ficam com um contexto neutro e restauram o contexto
   anterior quando terminam, inclusive em uma exceção testada em C++.

`modern_vegetation_view.h` reúne as regras da vegetação;
`modern_world_culling_view.h`, as regras da seleção de objetos;
`engine_render_core.h` seleciona a rota e `dxgi_proxy.cpp` liga essas regras às
chamadas do jogo. Escolhi manter a versão junto do contexto para impedir que
um ponto da versão antiga aceite dados da atualização atual. As atualizações
originais dos objetos do mundo continuam acontecendo uma vez.

A DLL compilou e **112/112 testes passaram em 14,03 segundos**. Os dois testes
de visibilidade fizeram **856 verificações na CPU**, usando dados fabricados:
658 da vegetação e 198 do mundo. Exercitam as duas versões, os dois olhos,
os limites geométricos da visão, as duas projeções, as etapas antecipadas e
tardias, misturas de versão, chamadas internas, argumentos e exceções.
Não executaram o jogo, a instalação real das ligações, a placa de vídeo,
o Quest ou o combate. Igualdade de dados lidos não conserva sozinha a vida
útil dos objetos nem prova a ordem do desenho.

**O bloqueio geral de Remastered e da reentrada continua fechado. Nada
experimental foi instalado; o VR ainda não funciona.** A organização e a
autoridade das câmeras da atualização atual ainda precisam de adaptação,
assim como os históricos, efeitos, recursos e a seleção do mesmo par de
imagens. Também falta garantir a vida útil durante desenho e encerramento.
Primeiro serão verificadas imagens reais no PC; depois, cabeça no Quest,
mãos, armas, dano e bloqueio físicos. DLSS e IntelFG continuam opcionais;
a meta de 60 FPS permanece cancelada. As análises novas do executável
continuam privadas locais.

## Captura sem antisserrilhamento vinculada ao olho — 9 de outubro

Liguei a identificação do desenho normal ao pedido de cópia da imagem daquele
olho. Antisserrilhamento é o tratamento que suaviza as bordas da imagem; esta
etapa usa a opção sem esse tratamento e não exige DLSS ou geração de quadros.

O fluxo, passo a passo:

1. Durante a chamada normal, guarda a versão conferida, o quadro, o olho,
   o par e a visão congelada da cabeça. Uma chamada interna rejeitada oculta
   essa marca; ao terminar, restaura exatamente o contexto anterior.
2. Quando o jogo grava a mudança do buffer da tela de escrita para apresentação,
   confere a marca daquela chamada e a lista DirectX concreta que a recebeu.
   DirectX é a interface usada para enviar o desenho à placa de vídeo.
3. Obtém a textura atualmente selecionada pela cadeia de apresentação, que
   alterna os buffers da tela. Mantém uma referência dessa textura enquanto
   grava a cópia. Texturas intermediárias com o mesmo tamanho não ganham
   autorização; mudanças parciais ou divididas também são rejeitadas.
4. Usa a cópia já existente para uma textura própria do mod e guarda um registro
   independente por cópia. Se os dois olhos usam a mesma lista, continuam com
   registros separados. A identificação não vem da fila antiga de tarefas
   terminadas nem da alternância do número de apresentações.
5. Encaminha esses registros à etapa existente de envio à GPU, com seus sinais
   de término. GPU é a placa de vídeo. Terminar uma função no processador do PC
   não publica uma imagem como pronta. A etapa consumidora ainda precisa
   respeitar os sinais e selecionar o par correto.

`modern_eye_capture.h` confere os dados do pedido; `dxgi_proxy.cpp` liga essas
regras às chamadas DirectX e à cópia existente. O teste novo exercita versões,
reutilização da mesma lista, chamadas internas, exceções e rejeições.
Escolhi copiar no ponto de gravação para conservar a associação entre o olho
conferido e aquela cópia antes que o jogo volte a escrever no buffer.

A DLL compilou e **112/112 testes passaram em 8,37 segundos**. O teste novo
fez **52 verificações na CPU**, com dados fabricados. Não executou o jogo,
MinHook, a gravação ou submissão DirectX deste caminho, a placa de vídeo,
o Quest ou o combate físico. Compilar uma chamada DirectX não prova que ela
produziu a imagem correta no jogo.

**Remastered e a reentrada continuam bloqueados. Nada foi instalado; o VR
não está jogável.** Ainda faltam confirmar a ordem e a vida útil no jogo,
o encerramento das chamadas, a seleção do par no consumidor e as demais
adaptações de câmera, visibilidade, efeitos e históricos por olho. A referência
obtida durante a gravação não resolve sozinha a vida útil até o término na GPU
ou durante redimensionamento e desligamento. Depois das imagens reais virão
cabeça no Quest, mãos, armas, dano e bloqueio físicos. A meta de 60 FPS
continua cancelada; DLSS e IntelFG permanecem opcionais. As análises novas
do executável permanecem privadas locais.

## Desenho normal identificado por versão — 8 de outubro

Adaptei a identificação da chamada principal de desenho e da tarefa que
continua esse desenho para a atualização 1048522. Cada perfil reúne os
endereços e os primeiros bytes examinados daquela versão. O perfil antigo
continua separado do novo.

O fluxo, passo a passo:

1. Escolhe um perfil conhecido a partir da regra de câmera. Regras desconhecidas
   ou copiadas não recebem autorização.
2. Reconhece o desenho normal somente quando a chamada vem do ponto esperado
   naquela versão e o registro do quadro tem olho, par e geração válidos.
   Chamadas privadas e chamadas da outra versão ficam sem a marca de olho.
3. Confere o tipo exato da tarefa normal em uma pequena cópia dos seus dados.
   Guarda junto o perfil que fez a conferência; outro perfil não pode reutilizar
   esse resultado para marcar uma tarefa.
4. Encaminha os três dados originais da chamada principal e o único dado
   original da tarefa, uma vez. Durante a chamada, conserva a marca daquele
   olho; uma chamada interna rejeitada oculta a marca anterior e a restaura
   exatamente ao terminar. Isso acompanha trabalho na CPU, o processador do PC.
5. Exige que a versão conferida seja a mesma do conjunto de adaptações instalado.
   O perfil novo ainda não autoriza instalar esse conjunto: vegetação e
   visibilidade também precisam da adaptação. Ele não cai no instalador antigo.

Escolhi vincular os registros à versão porque reconhecer uma função nova não
adapta automaticamente as outras funções que trabalham junto com ela.
A marca de olho não mantém objetos ou texturas vivos e o término da chamada
não significa que a placa de vídeo terminou a imagem.

A DLL de desenvolvimento compilou e **111/111 testes passaram em 16,85 segundos**.
Os casos usam dados e chamadas fabricados: conferem versões misturadas,
endereços de chamadas privadas, tarefas truncadas, limites de endereço,
perfil copiado, encaminhamento dos argumentos e restauração em chamadas
internas e exceções de C++. Não executaram o jogo, a instalação das adaptações,
a placa de vídeo, o Quest ou o combate físico.

**O bloqueio global permanece fechado para Remastered. Nada foi instalado no
jogo e o VR ainda não está jogável.** O próximo trabalho continua sendo ligar
as duas imagens reais aos recursos e às listas de desenho corretos, preservar
os históricos e a vida útil por olho e completar as adaptações necessárias.
Depois disso virão o teste de imagem/cabeça no Quest e a integração das mãos,
armas, dano e bloqueio físicos. DLSS e IntelFG são opcionais; a meta de 60 FPS
continua cancelada. As análises novas do executável permanecem privadas locais.

## Cópia da câmera identificada por versão — 8 de outubro

Adaptei a identificação das cópias da câmera para a atualização 1048522. O
perfil reúne as chamadas examinadas de cada versão; a sequência permanece
ligada ao perfil escolhido ao entrar na fábrica, que cria os dados de um quadro.

O fluxo, passo a passo:

1. Recebe uma regra de câmera conhecida e escolhe seu perfil de cópia.
   Regra desconhecida ou uma cópia da regra não ganha autorização.
2. Acompanha a cópia temporária, a cópia final e as duas câmeras internas,
   conferindo a origem, o destino, a ordem e as chamadas daquela versão.
   Essas duas câmeras pertencem a um quadro do jogo; os olhos do Quest
   precisam de dois quadros desenhados separadamente.
3. A reconstrução da câmera só é reconhecida quando vem da chamada e do
   destino esperados pelo mesmo perfil. Chamadas da outra versão não
   completam a sequência.
4. Se outra cópia acontece durante uma chamada, guarda o contexto anterior
   e o restaura exatamente ao terminar, incluindo versão, olho e par.
5. O registro da câmera anterior conserva a cópia dos campos definidos,
   os espaços reservados do destino e a regra de reinício do histórico.
   Os demais subsistemas ainda sem adaptação rejeitam a regra nova.

Escolhi guardar o perfil durante toda a sequência para impedir que endereços
examinados em versões diferentes sejam tratados como uma única operação.
Isso identifica chamadas; não mantém objetos ou texturas vivos nem comprova
que a placa de vídeo terminou de desenhar.

DLL de desenvolvimento compilada; **111/111 testes passaram em 12,35 segundos**.
Foram verificadas sequências completas dos dois perfis, chamadas misturadas
em cinco pontos, cópias aninhadas entre versões, restauração do contexto,
perfis desconhecidos e rejeição dos subsistemas ainda sem adaptação. As
cópias de registros também passaram por 21 casos com origem e destino
sobrepostos nas três regras de versão, além de reinício, limites e preservação
dos espaços reservados. Os testes usam dados e chamadas fabricados no PC.
Não executaram as funções nativas do jogo, os hooks, a placa de vídeo ou o Quest.

**O bloqueio global e os instaladores de hooks continuam iguais. Nada foi
instalado no jogo e o VR ainda não está jogável.** Continuam pendentes os
recursos e históricos por olho, a vida útil e o encerramento das chamadas,
a integração das duas imagens reais, cabeça, mãos/armas e dano/bloqueio
físicos. DLSS é complementar e a meta de 60 FPS continua cancelada.
As análises novas do executável permanecem somente nos arquivos locais.

## Identificação da fila por versão — 8 de outubro

A atualização do jogo exigiu separar a identificação da fila de desenho por
versão. A fila guarda comandos à espera de execução. O código agora carrega
junto com cada identificação um perfil: o conjunto de tipos e endereços
examinados para aquela versão.

O fluxo, passo a passo:

1. O chamador informa um perfil conhecido. O código confere os tipos do
   desenhador, da fila e do comando, além do espaço disponível para leitura.
   Um perfil desconhecido ou uma leitura incompleta é rejeitado sem trocar
   o resultado anterior.
2. A identificação capturada guarda esse perfil. Mesmo quando os endereços
   dos objetos coincidem, uma identificação de outra versão não é equivalente.
3. A função original de reserva também precisa estar vinculada ao mesmo
   perfil. As chamadas existentes continuam vinculadas à versão anterior;
   elas não adotam silenciosamente os dados da atualização.
4. Quando as conferências passam, chama a função original com a fila real e
   o mesmo tamanho de reserva, preservando seu retorno. O motor continua
   controlando reservas, cabeçalhos e publicação dos comandos.
5. As observações de reutilização e encerramento da fila conservam o perfil
   junto com a identificação. Isso ainda não mantém os objetos do jogo vivos.

Escolhi identificar tanto os dados quanto a função porque conferir só os
endereços dos objetos permitiria misturar uma fila nova com uma função da
versão anterior. O perfil é informação explícita desse subsistema; não é uma
opção de configuração que libera o jogo.

DLL de desenvolvimento compilada; **111/111 testes passaram em 14,95 segundos**.
O teste da fila passou **190 verificações**, incluindo 23 novas sobre mistura
entre versões, função de reserva incompatível, perfis desconhecidos, resultado
preservado após rejeição e observações de reutilização. São testes com dados e
funções fabricados; não executaram a fila nativa, seus hooks, a placa de vídeo
ou o Quest.

**O bloqueio global continua igual, admitindo somente a versão antiga já
suportada. Nada foi instalado no jogo e o VR ainda não está jogável.**
Os demais endereços e campos da atualização exigem conferência independente.
As notas anteriores sobre a Remastered são registros da versão então examinada;
não autorizam usar seus endereços na instalação atual. Continuam pendentes a
integração dos dois olhos, recursos e históricos, vida útil e encerramento das
chamadas, mãos/armas e dano/bloqueio físicos. DLSS é complementar e a meta de
60 FPS em Novigrad continua cancelada. Novas análises nativas ficam locais.

## Escrita do registro anterior da câmera — 8 de outubro

Adaptei a cópia do registro da câmera anterior para a Remastered. Esse registro
contém dados usados para comparar a posição atual com a anterior. A adaptação
conserva os espaços reservados que o jogo deixa intactos ao fazer essa cópia.

O fluxo, passo a passo:

1. Confere a regra da versão e todo o espaço de destino antes de escrever.
2. Guarda uma cópia temporária da origem. Mesmo se origem e destino ocuparem
   parte do mesmo espaço, escrever um campo não estraga os próximos campos.
3. Copia os campos definidos do registro e conserva os espaços reservados
   existentes no destino, além de todos os dados fora desse registro.
4. Na Remastered, também aceita um registro completo que manda reiniciar o
   histórico. Uma construção que falha continua devolvendo erro sem publicar
   dados incompletos. O chamador precisa respeitar esse erro.
5. A versão antiga conserva sua regra de cópia e de admissão do registro.

Escolhi copiar os campos definidos porque copiar o bloco inteiro alterava os
espaços que a rotina original da Remastered conserva. A cópia temporária evita
corromper a origem quando as regiões se sobrepõem.

DLL compilada; **111/111 testes passaram em 11,25 segundos**. Foram conferidas
14 cópias com regiões sobrepostas nas duas versões, além de reinício do
histórico, preservação dos espaços reservados, limites e rejeição de regra
desconhecida.
Os testes usam dados fabricados; não executam o jogo, suas funções nativas,
a placa de vídeo ou o Quest. Esta alteração na escrita não conecta por si só
os históricos dos dois olhos: ainda falta associar tempo, continuidade,
reinícios, ordem e recursos a cada imagem real.

**Ainda não está jogável; nada instalado e a 5.00c continua bloqueada.**
DLSS permanece complementar, a meta de FPS continua cancelada e mãos, armas,
dano e bloqueio físicos ainda exigem integração no jogo. Análises novas do
executável permanecem somente nos arquivos locais.

## Construção durante a ativação dos observadores — 8 de outubro

Fechei uma janela na ativação das três passagens que acompanham a fila do jogo.
A fila guarda tarefas à espera de processamento. Antes, uma construção iniciada
quando só parte das passagens estava ativa poderia ser aceita ao terminar.

O fluxo, passo a passo:

1. Ao entrar, verifica se as três passagens já estão ativas. Uma entrada cedo
   demais apaga a observação antiga daquele endereço, mas não recebe permissão.
2. Executa o construtor original uma vez, com os mesmos argumentos e resultado.
   O jogo continua construindo sua fila normalmente.
3. Ao sair, exige a permissão obtida na entrada e confere novamente a ativação
   antes de ler a fila. Terminar a instalação no meio da chamada não recupera
   uma entrada rejeitada.
4. Se a ativação fechar durante uma entrada aceita, cancela somente aquela
   construção. Um cancelamento antigo não pode apagar uma construção nova.
5. Só depois dessas conferências pode registrar a identificação que a preparação
   da segunda imagem exige. Isso ainda não mantém os objetos do jogo vivos.

Escolhi exigir autorização na entrada e na saída porque uma conferência somente
na saída esconderia a parte da construção que ocorreu sem todos os observadores.
As rotinas do jogo conservam sua execução; a restrição vale para a imagem extra.

DLL compilada; **111/111 testes passaram em 11,04 segundos**. O teste da fila
passou em **167 verificações no processador**, incluindo 17 novas sobre ativação
parcial, entrada rejeitada seguida de ativação, fechamento na saída, reutilização
do endereço e cancelamento antigo sem atingir uma nova construção.
Esses testes não executaram o jogo, suas funções nativas, a instalação real das
passagens, a leitura da memória do processo, a placa de vídeo ou o Quest.

**Ainda não está jogável; nada instalado e a 5.00c continua bloqueada.**
Continuam pendentes a vida útil, a ordem e os recursos de cada olho, antes da
primeira prova de duas imagens novas no jogo. Depois vem imagem/cabeça no Quest
e a integração de mãos, armas, dano e bloqueio físicos. DLSS é complementar;
a meta de FPS permanece cancelada. Novas análises nativas permanecem locais.

## Construção, parada e reutilização da fila — 8 de outubro

O mod agora exige uma construção observada da fila e uma identificação nova
para cada construção. Um endereço de memória pode ser reutilizado pelo jogo;
mesmo endereço e mesmos números não significam a mesma fila de antes.

O fluxo, passo a passo:

1. Ao entrar no construtor original, invalida a observação anterior daquele
   endereço e reserva uma identificação nova. Ainda não permite a imagem extra.
2. Chama o construtor original uma vez, conservando seus argumentos e resultado.
   Nas duas rotas admitidas, confere a fila e seu responsável depois da construção.
   Nesse momento, o chamador ainda não guardou a fila em seu campo normal;
   a conferência usa o responsável que o próprio construtor registrou.
3. Só conclui aquela identificação se a construção ainda for a mesma e estiver
   válida. Uma parada durante a construção impede que ela seja reaberta depois.
   Rejeitar uma construção antiga não cancela uma nova que reutilizou o endereço.
4. Antes da rotina original de parada ou destruição observada, fecha a observação.
   O jogo conserva seus comandos, esperas, argumentos de destruição e resultado.
   O mod não mantém seu bloqueio interno enquanto essas rotinas originais executam.
5. A preparação das duas imagens exige a mesma identificação da fila. Uma fila
   não observada, substituída ou parada impede a preparação extra. As três
   passagens precisam estar instaladas e conferidas para permitir essa adaptação.

Escolhi uma identificação por construção porque comparar só endereços e números
poderia aceitar uma nova alocação como se ainda fosse a antiga. O registro usa
armazenamento limitado e fecha a permissão em vez de repetir identificações.
Também corrigi a falha parcial das passagens de preparação: conserva os caminhos
originais, cancela sua ativação e impede nova tentativa no conjunto parcial.

Isso é uma observação, não uma reserva da vida útil da fila: ela pode parar depois
da conferência. Ainda faltam todas as rotas de liberação, a proteção das chamadas
em andamento, a ordem e o consumo dos dados de cada olho e o término do uso pela
placa de vídeo. Conservar caminhos originais não prova parada segura da DLL.
Filas já existentes quando esses observadores entram não são aceitas por amostragem.

DLL compilada; **111/111 testes passaram em 10,68 segundos**. O teste da fila
passou em **150 verificações no processador**, incluindo construção ainda sem o
campo publicado, endereço reutilizado com dados idênticos, cancelamento antigo,
limites do registro, identificação sem repetição e parada concorrente à conclusão.
Não executaram o jogo, suas funções nativas, as leituras do processo, a instalação
ou falha real de MinHook, a placa de vídeo nem o Quest.

**Ainda não está jogável; nada instalado e a 5.00c continua bloqueada.**
A próxima etapa continua sendo fechar a vida útil e a ordem dos recursos para
verificar duas imagens novas por quadro no jogo e depois imagem/cabeça no Quest.
Mãos, armas, dano e bloqueio por contato continuam exigindo integração real.
DLSS é complementar; a meta de FPS permanece cancelada. Novas análises nativas
permanecem somente nos arquivos locais.

## Segunda imagem vinculada à fila da primeira — 8 de outubro

A reserva do comando extra agora usa explicitamente a fila identificada na
construção da primeira imagem. Antes, procurava novamente a fila global, o que
poderia selecionar outra fila se o jogo a trocasse entre as duas chamadas.
Uma fila é o conjunto de tarefas que aguardam processamento pelo jogo.

O fluxo, passo a passo:

1. Ao preparar o par de imagens, confere o responsável pelo desenho, a classe
   da fila, seu espaço de memória e os limites que o jogo registrou. Guarda
   somente essa identificação; não cria uma fila nem copia objetos do jogo.
2. Quando o jogo constrói o comando da primeira imagem, confere que ele está
   no espaço daquela fila, contém a imagem esperada e ainda não foi liberado
   para processamento. Comandos de outras imagens ou já liberados não servem.
3. Antes da reserva extra, lê novamente a identificação e os dados da cena.
   Os contadores de tarefas e a posição de leitura podem mudar normalmente;
   eles não são confundidos com uma troca de fila.
4. Usa a função original de reserva com o endereço daquela fila. O jogo
   continua administrando seus espaços, retornos ao começo da memória,
   construção dos comandos, referências e liberação. Limites negativos ou
   insuficientes e uma fila diferente impedem essa reserva extra.
5. Se rejeitada, a rotina existente cancela a segunda imagem e libera somente
   a referência que pertence ao mod. A imagem original permanece com o jogo.

Escolhi vincular a reserva ao receptor já conferido para não depender de uma
segunda busca global durante o envio. Isso não reserva a vida útil da fila,
comprova consumidor único nem encerra o uso dos recursos pela placa de vídeo.
A capacidade mínima conferida não garante espaço para todas as tarefas internas
que a preparação da imagem possa criar. Esses pontos continuam pendentes.

DLL compilada; **111/111 testes passaram em 10,84 segundos**. O teste novo
passou em **47 verificações no processador**: identidade, mudança de fila,
contadores que podem variar, limites negativos/insuficientes, borda de retorno
da memória, comando de outra imagem ou já liberado e preservação dos argumentos
e do retorno da função de reserva. Usa dados e funções de teste; não executou
o jogo, suas funções nativas, leituras do processo ou o Quest.

**Ainda não está jogável; nada instalado e a 5.00c continua bloqueada.**
Faltam vida útil, consumo e ordem das tarefas/recursos, demais efeitos,
históricos e parada segura, antes de verificar duas imagens novas por quadro
no jogo e imagem/cabeça no Quest. Mãos, armas, dano e bloqueio físico continuam
exigindo integração real. DLSS complementar; meta de FPS cancelada. Dados
novos da análise nativa permanecem somente nos arquivos locais.

## Câmera do olho na seleção de objetos do mundo — 8 de outubro

A seleção de objetos do mundo agora encaminha a câmera principal de cada olho
para uma cópia privada que o jogo já constrói. Essa cópia conserva a lente do
olho, em vez de começar com a lente secundária usada no monitor. Uma cópia
privada é uma câmera temporária usada somente nessa tarefa do jogo.

O fluxo, passo a passo:

1. A tarefa recebe do próprio jogo a cena e os dados da imagem que está
   processando. Confere a identificação dessa imagem, o olho e a sessão atual;
   não pega a identificação de outra tarefa executada em paralelo.
2. Confere que a cena pertence ao mesmo responsável pela seleção dos objetos.
   Uma chamada desconhecida usa uma identificação neutra durante sua execução
   e restaura a anterior ao terminar.
3. Somente na cópia específica dessa tarefa, verifica a origem secundária e
   o destino separado das duas câmeras. Leituras repetidas precisam coincidir,
   e os dados básicos da lente precisam ser válidos.
4. Entrega à função original o endereço da câmera principal daquele olho.
   As cópias de números usadas na conferência não viram objetos do jogo.
   O jogo conserva o destino, o resultado, a reconstrução e a liberação da
   câmera temporária. O movimento da cabeça não é aplicado uma segunda vez.
5. Se alguma conferência falhar, chama o caminho original com sua câmera
   original. As cinco passagens do grupo precisam estar instaladas e
   conferidas antes de permitir a adaptação.

Escolhi trocar a origem dessa cópia porque isso deixa o próprio jogo calcular
os dados derivados e cuidar da câmera temporária. Copiar seus bytes para um
objeto inventado pelo mod não conservaria as referências que o jogo administra.

DLL compilada; **110/110 testes passaram em 10,92 segundos**. O teste novo
passou em **80 verificações no processador**: duas lentes, bordas de objetos
visíveis, origem real da câmera, argumentos e resultado originais, rejeição de
dados inválidos, restauração após chamadas internas e separação entre tarefas.
Não executaram o jogo, suas funções nativas, a instalação das passagens ou o
Quest. Leituras repetidas iguais não comprovam posse nem reserva dos objetos.

**Ainda não está jogável; nada instalado e a 5.00c continua bloqueada.**
Faltam a ordem e a vida útil dos dados compartilhados, outros consumidores de
visibilidade, históricos, efeitos, recursos e parada segura. Depois verificar
as duas imagens novas por quadro no jogo e imagem/cabeça no Quest. Mãos, armas,
dano e bloqueio físico continuam exigindo integração real. DLSS complementar;
a meta de FPS permanece cancelada. Novos dados nativos continuam somente locais.

## Vegetação na etapa antecipada da câmera — 8 de outubro

A adaptação agora cobre também as duas atualizações antecipadas de vegetação
na chamada normal que desenha a imagem. Antes, somente as duas posteriores
recebiam a câmera principal de cada olho. As quatro usam a posição, a direção,
a variante correta da lente e a distância mínima da câmera daquele olho.

O fluxo, passo a passo:

1. A chamada normal identifica o olho, a imagem e a sessão de VR atual.
2. Ao entrar na etapa antecipada, confere a origem da chamada, a mesma cena,
   o conjunto de dados de vegetação dessa cena e o descritor daquela imagem.
   Um descritor é o conjunto de informações que o jogo recebe para desenhá-la.
3. Cria uma identificação temporária dessa etapa. Somente as suas duas
   atualizações podem usar essa identificação; as duas posteriores continuam
   exigindo o contexto normal. Uma chamada desconhecida recebe um contexto
   neutro, mesmo quando acontece dentro de outra chamada identificada.
4. Cada atualização confere os dados originais e usa cópias próprias dos
   números da câmera principal. Conserva os demais argumentos, o resultado e
   a quantidade de chamadas da função original. A etapa antecipada conserva
   seus três argumentos e o agendamento original do jogo.
5. Ao sair, restaura a identificação anterior, incluindo o olho e a etapa.
   As quatro passagens deste grupo precisam estar instaladas e conferidas
   antes de permitir a adaptação. Falha parcial mantém a permissão fechada.

Escolhi uma identificação separada para a etapa antecipada porque apenas
estar dentro do desenho normal não identifica qual chamada interna pode
receber a câmera de VR. Isso evita aplicar a correção a outra rota do jogo.

DLL compilada; **109/109 testes passaram em 10,36 segundos**. Os testes de
vegetação passaram em **292 verificações no processador**: quatro chamadas,
dois olhos, bordas de lentes distintas, preservação de argumentos e resultados,
rejeição de cenas/imagens/etapas diferentes e restauração após chamadas internas
e uma exceção de teste. Uma exceção é uma interrupção controlada do código.
Não executaram o jogo, suas funções nativas, MinHook nem o Quest.

**Ainda não está jogável; nada instalado e a 5.00c continua bloqueada.**
Este passo não separa os objetos compartilhados de vegetação nem comprova a
ordem e a vida útil das tarefas do jogo. Continuam pendentes esses pontos,
a visibilidade restante, efeitos, históricos, recursos e parada segura.
Depois verificar duas imagens novas por quadro no jogo e imagem/cabeça no
Quest, antes de integrar mãos, armas, dano e bloqueio por contato físico.
DLSS continua complementar; a meta de FPS permanece cancelada. A nova análise
nativa permanece somente nos arquivos locais, sem publicação desses dados.

## Vegetação recebe a câmera do olho correto — 8 de outubro

Integrei o encaminhamento da câmera principal de cada olho nas duas chamadas
normais que atualizam a seleção de vegetação. Isso evita usar a lente original
da câmera secundaria para essas chamadas. O restante da seleção de objetos
visiveis, as tarefas adiadas e o isolamento dos dados ainda estao pendentes.

O fluxo, passo a passo:

1. A entrada normal que desenha a cena identifica o quadro, o olho e a sessão
   de VR. Essa identificação vale somente durante a chamada atual. Entradas
   desconhecidas e chamadas internas de outras rotas não herdam essa permissao.
2. Nas duas chamadas de vegetação verificadas, confere qual objeto recebera
   os dados. Lê cópias limitadas das duas câmeras daquele mesmo quadro e
   verifica se outra leitura entrega os mesmos bytes. Isso detecta mudancas
   durante a leitura; não prova que o objeto esteja reservado para o mod.
3. Compara os dados recebidos pela chamada com a câmera secundaria. Se não
   forem exatamente os esperados, encaminha os dados originais sem correção.
4. Usa a posição, a direção de visão, a variante correta da lente e a distancia
   mínima da câmera principal daquele olho. Guarda esses números em cópias
   próprias que sobrevivem ate a função original terminar. Nao altera os
   bytes de nenhuma das duas câmeras.
5. Chama a função original uma vez. Conserva o objeto receptor, o alcance da
   grade de vegetação, o modo e o resultado original. O alcance da grade
   continua sendo o do jogo; não é substituído pela distancia máxima da lente.
6. As tres passagens deste grupo só permitem a adaptacao depois de instaladas
   e conferidas. Em falha parcial, a permissao fica fechada e os caminhos
   originais são conservados. Isso ainda não resolve a parada segura do mod.

Escolhi encaminhar os dados da câmera principal porque ela ja recebe a lente
assimétrica de cada olho. Copiar apenas o campo de visão para a secundaria não
alinharia a visibilidade quando as câmeras tem posicoes e direcoes diferentes.
A função do jogo continua calculando seus planos e sua seleção de vegetação.

A DLL compilou e **109/109 testes passaram em 10,28 segundos**. O teste novo passou em **140 verificações no processador**:
duas lentes e duas variantes de projeção, bordas que a câmera secundaria
exclui, preservacao do alcance e retorno, cópias independentes das fontes,
rotas rejeitadas e restauracao da identificação após chamadas internas.
O modelo de recorte de imagem é independente do encaminhamento testado.
Nao executou a função nativa de vegetação, o jogo, MinHook ou o Quest.

**Ainda não está jogável; nada instalado e a 5.00c continua bloqueada.**
Falta provar quando cada tarefa consome os dados de vegetação compartilhados,
separa-los ou ordenar seu uso por olho, adaptar os demais consumidores de
visibilidade, historicos e efeitos e resolver a vida útil e a parada dos
recursos. Depois verificar duas imagens novas por quadro no jogo e realizar
o primeiro teste de imagem/cabeça no Quest. Mãos, armas e combate físico ainda
precisam de integracao real. A meta de FPS continua cancelada.

## Lente própria para cada olho — 7 de outubro, noite

O preparo das cópias privadas agora ajusta também a lente da câmera principal
para cada olho, usando a mesma leitura OpenXR congelada que fornece a cabeça.
OpenXR é a ligação que entrega as posições e a abertura de visão do headset.

O fluxo, em linguagem simples:

1. Confere a câmera original, a distância mínima/máxima de desenho e os quatro
   ângulos que o Quest fornece para a lente de cada olho. Um preparo inválido
   conserva as duas saídas anteriores e não chama nenhuma fábrica.
2. Calcula a abertura vertical e a proporção horizontal da imagem para cada
   lente. Substitui o zoom da câmera do monitor pela lente física do headset.
3. Grava o centro óptico, que é o deslocamento da visão em relação ao centro da
   imagem, em um campo estável separado da pequena oscilação dos efeitos
   temporais. Usa um valor absoluto: não soma outra vez ao centro anterior.
4. Altera esses cinco números só na câmera principal de cada cópia privada.
   Conserva distâncias de desenho, oscilação em pixels, dimensões inteiras,
   histórico, matrizes derivadas e referências de outros objetos. O jogo
   reconstrói as matrizes ao copiar as entradas de cada olho.
5. Na passagem moderna que atualiza a oscilação temporal, encaminha os cinco
   argumentos originais uma vez e retorna antes das correções da versão
   antiga. Uma atualização temporal não deve apagar nem duplicar a lente.

Escolhi o campo estável porque ele é aplicado separadamente na reconstrução da
câmera moderna. Misturar o centro óptico com a oscilação temporal exigiria
repor esse centro em cada atualização do jogo e poderia acumulá-lo nas rotas
de restauração. A câmera interna secundária conserva sua lente original nesta
etapa; sua função de escolher os objetos visíveis ainda precisa de adaptação.

A DLL compilou. **108/108 testes passaram em 10,54 segundos**, incluindo
**424 verificações de câmera/lente**. Um modelo de matriz independente confere
as quatro bordas de duas lentes diferentes, várias distâncias, profundidade,
zoom antigo, centro anterior não nulo, oscilação temporal e dimensões inteiras.
Os testes também conferem todos os bytes fora das escritas permitidas e as
saídas preservadas em rejeições. Não executaram a função nativa de câmera,
o jogo, a instalação das passagens nem o headset.

**Continua sem versão jogável; nada instalado e 5.00c bloqueada.** Ainda faltam
visibilidade para as lentes assimétricas, histórico separado por olho, demais
efeitos, propriedade/vida útil/ordem dos recursos e tarefas e a parada segura.
Depois provar duas imagens novas por quadro no jogo, testar cabeça/imagem no
Quest e integrar mãos, armas e contato/dano/bloqueio físicos. Não há prazo
confiável, porcentagem de conclusão ou meta de FPS. DLSS é complementar.
Código novo: `src/modern_camera_projection.h`; integração no preparo de pose e
na passagem temporal. Evidências nativas novas permanecem somente locais.

## Pose do headset nas entradas privadas de câmera — 7 de outubro, noite

Implementei o preparo da posição e da rotação para os dois olhos, antes de
criar qualquer um dos quadros. É código integrado ao caminho moderno da
fábrica de quadros, ainda atrás do bloqueio de compatibilidade da 5.00c.

O fluxo, em linguagem simples:

1. O OpenXR, que fornece o rastreamento do Quest, entrega as duas visões juntas.
   O código guarda essa leitura, seu horário previsto e a origem usada para
   recentralizar. A fábrica exige a mesma geração e o horário do par pedido.
2. Cria duas cópias privadas dos dados de entrada. Os dados originais do jogo
   ficam intactos. Calcula os dois olhos antes de chamar a primeira fábrica.
3. Converte direita/cima/trás do headset para os eixos do jogo. Soma o movimento
   da cabeça à câmera e conserva a distância física entre os olhos. Ajustar a
   escala do deslocamento da cabeça não aumenta nem diminui essa distância.
4. Aplica a mesma transformação ao conjunto das duas câmeras internas de uma
   cena, preservando sua posição e sua orientação relativas. Essas câmeras
   internas não são os dois olhos: cada olho recebe seu próprio quadro.
5. Muda somente seis números de pose por câmera. O campo de visão, as matrizes,
   o histórico e as referências de outros objetos continuam intactos nas
   cópias. O jogo reconstrói as matrizes quando copia essas novas entradas.
   As passagens de cópia e reconstrução não somam a pose novamente.
6. Guarda a mesma leitura congelada nas identificações dos dois quadros. Se o
   preparo falhar antes das fábricas, usa a entrada original. Se a sequência ou
   geração falhar depois, não admite o par; o quadro já criado pode conter a
   pose de um olho e não é apresentado como uma imagem original sem alteração.

Escolhi preparar cópias antes da fábrica porque ela reconstrói as câmeras mais
uma vez ao criar o quadro final. Corrigir a cabeça durante ambas as cópias
repetiria a rotação e o deslocamento. As cópias privadas também permitem
rejeitar um preparo incompleto antes de alterar qualquer entrada do jogo.

A DLL compilou. **108/108 testes passaram em 12,36 segundos**; o teste novo tem
**351 verificações no processador**: eixos, recentralização, distância entre
olhos, câmeras com bases diferentes, inclinação dos olhos, ângulos verticais,
dados inválidos e preservação de todos os bytes fora da pose. As matrizes de
referência usam fórmulas trigonométricas independentes. Não executaram o jogo,
funções nativas, o headset nem a instalação das passagens.

**Ainda não está jogável e nada foi instalado.** O preparo mantém a projeção
original; falta portar o campo de visão/centro óptico para cada olho, os
históricos, a visibilidade e demais efeitos, a vida útil/ordem dos recursos e
as tarefas e sua parada segura. O bloqueio global da 5.00c permanece igual.
Depois verificar imagens novas nos dois olhos a cada quadro no jogo, fazer o
primeiro teste no Quest e integrar mãos, armas e contato/dano/bloqueio físicos.
O primeiro teste de imagem poderá preceder o combate pronto; ainda não há data
confiável. Não existe meta de FPS. DLSS permanece complementar.
Código: `src/modern_camera_pose.h`; integração: `src/dxgi_proxy.cpp`.
Evidências e recibo desta rodada ficam na pasta local `../artifacts`.

## Entrada normal e tarefa de desenho da 5.00c — 7 de outubro

Adaptei a entrada principal do renderizador e a tarefa de epílogo normal. O
renderizador é a parte que transforma a cena do jogo em uma imagem. O epílogo é
uma tarefa posterior desse desenho; pode trabalhar em outra linha de execução
(uma sequência de trabalho do processador). Não basta guardar o olho apenas na
entrada principal, pois esse dado não passa automaticamente para outra linha.

O código funciona assim:

1. Escolhe a entrada pela versão examinada. Na 5.00c usa a entrada real de três
   argumentos: renderizador, quadro e cena. Encaminha os três uma vez, inclusive
   quando a cena é vazia. Não executa as correções de memória da entrada antiga.
2. Aceita uma identificação de olho somente para o quadro encontrado no registro,
   na geração atual, com par e visão válidos e a sequência moderna de cópias
   concluída. A fábrica apaga a identificação antiga do endereço que acabou de
   devolver, antes de guardar uma nova. Isso reduz herança por reuso do endereço;
   ainda não prova a vida útil de todos os objetos nativos.
3. Na tarefa posterior, confere seu tipo exato e lê os endereços do renderizador
   e do quadro nas posições verificadas. A tarefa de supersampling (imagem maior)
   e tipos desconhecidos não recebem a identificação da tarefa normal.
4. Durante cada chamada, conserva olho, geração, par, visão, projeção, pose de
   cabeça e indicadores temporários. Uma chamada desconhecida dentro de outra
   recebe estado vazio. Quando termina, restaura todos os dados anteriores.
5. Confere os bytes das duas entradas e a ligação da tarefa normal antes de
   instalar. Só considera o conjunto pronto quando as duas ativações terminam.
   Em falha parcial, fecha a admissão e conserva os encaminhamentos originais.
6. A função original continua desenhando. Voltar de uma chamada do processador
   não significa que a placa de vídeo terminou a imagem; esta etapa não publica
   uma conclusão nem aplica a pose do headset.

Escolhi entradas separadas porque a versão antiga tem outras posições de câmera
na memória e caminhos que anunciam conclusão. Reutilizar aquele corpo inteiro
poderia escrever nos dados errados ou declarar pronta uma imagem ainda em uso.

A DLL compilou e passaram **107/107 testes em 8,83 segundos**. Os casos novos
usam dados fabricados: argumentos de 64 bits, cena vazia, dois olhos, chamadas
aninhadas aceitas/rejeitadas, dados antigos ou incompletos, leitura curta, tipo
de tarefa incorreto e restauração após exceção de C++. Não executam o Witcher,
a instalação dos hooks, falhas do MinHook, o headset ou imagens reais de jogo.

A análise estática confirmou o caminho da entrada principal para o emissor de
tarefas e a tarefa normal que chega aos consumidores da câmera. A função antes
chamada de candidata de pipeline foi identificada como emissor de tarefas;
a entrada real de DLSS permanece aquela examinada separadamente. Também
conferi o bloco de dados de câmera copiado pelo jogo para a placa de vídeo.
Esses achados e as instruções nativas ficam somente locais em
`../artifacts/remastered-normal-camera-consumer-evidence.json`.

**Ainda não está jogável: 5.00c e repetição permanecem bloqueados, nada instalado.**
Faltam a transformação de cabeça nas câmeras internas e suas matrizes, a
visibilidade, os demais efeitos, a vida útil/ordem das tarefas e recursos e a
parada segura das ligações. Depois comprovar imagens novas nos dois olhos em
cada quadro e integrar mãos, armas e combate físico ao jogo. Não existe meta
obrigatória de FPS; DLSS continua complementar.
Recibo local: `../artifacts/render-core-epilogue-port-validation.json`.

## Identificação das duas etapas de cópia da câmera — 7 de outubro

O código agora acompanha o caminho da câmera dentro da criação de um quadro.
“Contexto” é uma anotação temporária que só vale durante aquela chamada. O fluxo:

1. Abre um contexto para uma criação autorizada de quadro, com o descritor de
   entrada, o olho e o número do par. Uma chamada dentro de outra recebe contexto
   próprio e recupera o anterior quando termina.
2. Identifica a primeira cópia para a área temporária, conferindo origem,
   destino, local de chamada e ordem. Acompanha as duas câmeras internas dessa
   cópia; elas pertencem a uma cena e não representam os dois olhos do Quest.
3. Só aceita a segunda cópia quando sua origem é a área temporária já concluída.
   Confere as duas reconstruções de câmera e o resultado devolvido pela cópia.
4. Ao terminar, confere que a cópia final está dentro do quadro realmente
   devolvido. Uma sequência incompleta impede a admissão do quadro adicional;
   o resultado original da primeira criação continua sendo encaminhado.
5. Na 5.00c, a reconstrução observada segue para a função original uma vez.
   Ainda não aplica pose de cabeça nem entra nas correções antigas de câmera.

Escolhi acompanhar toda a sequência porque o mesmo endereço de reconstrução
aparece nas duas etapas e nas duas câmeras. Ele sozinho não identifica a origem
nem permite saber se uma futura correção de cabeça já foi aplicada. As ligações
novas só são consideradas prontas depois das três instalações bem-sucedidas,
com conferência de bytes. Uma instalação parcial mantém a admissão fechada e
conserva as funções de encaminhamento; ainda falta resolver sua parada completa.

A DLL compilou e passaram **107/107 testes em 7,18 segundos**. Os novos casos
usam chamadas e endereços fabricados, incluindo ordem errada, origem incorreta,
cópias sobrepostas, reconstrução ausente/repetida, resultado divergente,
chamadas aninhadas e interrupção por exceção de C++. Não executam o jogo,
funções nativas, poses no headset ou produção de imagens.

**Ainda não está jogável; nada foi instalado e 5.00c continua bloqueado.**
A identificação não comprova memória válida, vida útil dos recursos, ordem
entre tarefas ou segurança ao descarregar a DLL. O próximo passo é verificar
como o desenho normal usa as duas câmeras internas e adaptar matrizes,
visibilidade e efeitos para os dois olhos. Depois integrar cabeça, Touch e
combate físico. A meta de FPS permanece cancelada; DLSS é complementar.
Evidência somente local: `../artifacts/camera-copy-context-port-validation.json`.

## Centro da projeção e escrita da pose — 7 de outubro

O caminho que reconstrói a câmera agora usa as posições de memória da versão
escolhida para o centro da imagem e suas dimensões. “Projeção” é o cálculo que
transforma o mundo 3D na imagem vista por um olho. O fluxo ficou assim:

1. Seleciona o formato conhecido da câmera. Se não reconhecer, encaminha a
   chamada original sem tentar modificar esses dados.
2. Grava o centro como dois valores com casas decimais e largura/altura como
   dois números inteiros. O caminho manual também passou a preservar esse tipo.
3. Depois da reconstrução, lê as dimensões na posição correta para conferir o
   resultado. A cópia usada pelos marcadores recebe a mesma posição por versão.
4. Ao ajustar um olho com orientação inclinada, copia somente o tamanho de
   câmera registrado para a versão e grava apenas posição e rotação: seis
   valores. Conserva campo de visão, matrizes, histórico e dados vizinhos.

Escolhi limitar a escrita porque a rotina anterior copiava de volta um bloco
inteiro de 2.048 bytes, apesar de mudar somente a pose. Também limitei as cópias
locais de marcadores e da base dos olhos ao tamanho registrado da câmera.

A DLL compilou e passaram **107/107 testes em 9,07 segundos**. Os novos casos
conferem dimensões inteiras, preservação dos demais bytes, campos inválidos,
leituras curtas e as duas câmeras internas de um único descritor. São dados
preparados para teste; essas duas câmeras não são imagens do headset.

**5.00c continua bloqueado e nada foi instalado.** Faltam o contexto das duas
etapas de cópia da câmera normal, demais matrizes/efeitos, renderização dos
dois olhos por quadro e integração física com o jogo. As outras rotas de câmera
não foram declaradas adaptadas. Leituras protegidas e limites de tamanho não
comprovam vida útil dos objetos, ordem das tarefas nem escrita atômica.
Evidência local: `../artifacts/camera-projection-port-validation.json`.

## Funções de cabeça e autoridade da câmera por versão — 7 de outubro

A adaptação agora escolhe nove funções de cabeça e câmera conforme a versão
verificada do jogo. “Autoridade da câmera” significa identificar qual câmera
está controlando a visão, para não confundir a câmera de jogo com uma cena.
O código segue este fluxo:

1. Escolhe o conjunto de endereços da versão reconhecida. Um conjunto
   desconhecido não recebe endereços para chamar.
2. Na 5.00c, confere os primeiros bytes de cada função antes de preparar a
   ligação. Se diferirem, recusa aquela ligação e preserva a função original.
3. Lê o estado de controle manual na posição correta da 5.00c. A posição mudou;
   outros campos examinados continuam no mesmo lugar. Não desloca tudo junto.
4. Identifica o tipo principal da câmera ativa. Partes secundárias do mesmo
   objeto e outros tipos de câmera não recebem essa identificação.
5. Continua encaminhando as chamadas existentes para as funções originais.
   Estas leituras não escrevem nos estados de câmera nem ativam o VR.

Escolhi separar endereços e campos por versão porque reutilizar os valores da
4.04 poderia ler dados errados ou chamar outra função. Dois métodos tinham o
mesmo nome; selecionei o que pertence ao grupo normal do diretor de câmeras.

A DLL compilou e passaram **107/107 testes em 6,81 segundos**, incluindo versões
não reconhecidas, bytes divergentes, leituras curtas e a posição antiga usada
como armadilha. Esses casos usam dados preparados para teste. A evidência do
executável foi obtida por leitura do arquivo: não executei as funções no jogo.

**Nada foi instalado e 5.00c continua bloqueado.** Esta etapa não comprova vida
útil dos objetos, ordem entre tarefas do jogo, poses no Quest, imagens nos dois
olhos ou combate físico. A prioridade seguinte é adaptar o caminho normal de
câmera, matrizes e desenho do mundo que a primeira integração estéreo precisa.
DLSS continua complementar. A meta de FPS em Novigrad permanece cancelada.
Evidência local: `../artifacts/camera-authority-port-validation.json`.

## Identificação preservada ao recriar o observador — 7 de outubro

A lista gráfica pode continuar existindo enquanto o componente que a acompanha
é recriado. Antes, esse componente começava seu contador de novo; duas gravações
poderiam receber o mesmo identificador. O código agora segue este fluxo:

1. Mantém o histórico junto da identidade real da lista e da placa. Pacotes de
   tarefas antigos também guardam esse histórico, mesmo sem o observador antigo.
2. Ao criar outro observador, conserva o contador e começa com estado desconhecido.
   Precisa observar um Reset real bem-sucedido para aceitar outra gravação.
3. A gravação nova recebe o número seguinte. Dados antigos e avisos atrasados
   não podem ser tratados como parte dessa gravação.
4. As tarefas já enviadas continuam guardando suas texturas até a confirmação
   própria da GPU. Trocar o observador não libera recursos ainda em uso.

Escolhi ligar o histórico ao objeto gráfico, em vez de ao componente temporário,
para evitar reiniciar a identificação por acidente. As travas internas protegem
esse histórico; ainda não comprovam a ordem das chamadas do Witcher.

A DLL compilou e passaram **107/107 testes em 11,96 segundos**. Na RTX 4070 Ti
passaram **28 cópias reais**. A nova cópia ficou pendente enquanto destruí e recriei
o observador; a gravação passou de 1 para 2, recusou dados antigos e os pixels B8
chegaram corretamente após a confirmação da GPU.

Isso ainda é um teste independente. Recibos e olhos são fabricados; não executa
Witcher, DLSS ou Quest. Nada foi instalado e 5.00c continua bloqueado. Não comprova
segurança ao descarregar a DLL nem o funcionamento das mãos e do combate físico.
Evidência local: `../artifacts/modern-dlss-recording-clock-port-validation.json`.

## Convivência com os comandos gráficos antigos — 7 de outubro

O observador novo agora pode trabalhar com os caminhos de Reset e Execute
que o próprio mod já instalou. Reset começa uma gravação de tarefas da placa;
Execute envia essas tarefas. O fluxo ficou assim:

1. O caminho antigo guarda uma ligação com sua função original depois que
   sua instalação dá certo. Essa ligação é publicada de modo seguro para
   as atividades paralelas do programa.
2. A parte nova só usa a ligação se o endereço pertencer à função nativa
   correta do Direct3D. Um objeto intermediário não vale como essa prova.
3. Observa antes e depois, preservando a chamada original uma única vez.
   O caminho antigo continua fazendo suas atualizações de estado.
4. Se a ligação deixa de valer, a observação para. Ao retirar o observador,
   o código conserva a instalação antiga e sua função original.

Escolhi compartilhar o caminho que conhecemos para evitar duas instalações
disputando a mesma função. Instalações desconhecidas continuam recusadas.
As tarefas internas do ReShade continuam isoladas: seguem diretamente para
o caminho original e invalidam a observação nova, pois ela seria incompleta.

A DLL compilou e passaram **107/107 testes em 11,69 segundos**. Na RTX 4070 Ti
passaram **27 cópias reais**, incluindo uma cópia pelo caminho compartilhado.
Nesse caso houve um Close, dois Reset e um Execute observados, cada chamada
encaminhada uma vez. Os pixels foram conferidos após a confirmação própria
da placa; o caminho antigo continuou funcionando depois da retirada nova.

Isso ainda não ativa o VR no Witcher. Falta comprovar a parada e a ordem das
chamadas do jogo, conectar o registro de produção e separar os recursos dos
olhos. Recibos e olhos no teste são fabricados; Witcher, DLSS e Quest não foram
executados. Nada foi instalado; o bloqueio de 5.00c continua fechado.
Evidência somente local: `../artifacts/modern-dlss-cooperative-hooks-port-validation.json`.

## Observação das funções nativas da GPU — 6 de outubro

Agora existe `modern_dlss_native_hooks`, uma entrada nas funções reais que
fecham, reabrem e enviam listas de tarefas. Um hook é uma passagem que permite
observar uma chamada e continuar para a função original. O código faz isto:

1. Confirma os objetos nativos da placa e conserva suas referências. As
   funções escolhidas precisam pertencer ao Direct3D do Windows, sem usar
   a camada intermediária como prova de identidade.
2. Cria os três observadores e só aceita observações depois de ativar todos.
   Se já existir outro hook num endereço, recusa o grupo e preserva o outro.
3. Observa antes e depois, encaminhando a chamada original uma única vez.
   Mantém a mesma fila, a mesma lista de tarefas, a quantidade e o resultado.
4. Na retirada, desativa a observação e conserva a passagem original enquanto
   uma chamada estiver em andamento. Para removê-la, o responsável precisa
   comprovar que todas as threads — atividades paralelas do programa — pararam
   de chamar essas funções. Um contador igual a zero, sozinho, não comprova isso.

Escolhi observar a entrada nativa para evitar a confusão entre a lista real
e seus wrappers, objetos intermediários usados por outros componentes.
No caminho moderno do Witcher, acrescentei a aquisição da lista **antes** da
produção pelo SDK, o componente da NVIDIA, e a comparação com a identidade
depois. Essa comparação ainda não comprova uma gravação nova nem sua execução.
O instalador nativo não é ativado automaticamente no jogo: ainda faltam a
coordenação com os hooks antigos, a parada das chamadas e o ciclo de vida.

Na RTX 4070 Ti, os observadores acompanharam **31 Close, 31 Reset e 26 Execute**,
com 88 pares de eventos antes/depois. Close fecha a gravação; Reset a reabre;
Execute a envia. O registro passou a receber os resultados reais de Close e
Reset; os eventos de Execute guardaram os recursos antes da chamada original
e pediram a confirmação da GPU depois. Passaram as 26 cópias, o reenvio, a
retenção durante encerramento, as falhas reais de Close/Reset, a recusa de
conflito e a retirada durante uma chamada ativa.

A DLL compilou; **107/107 testes em 14,88 segundos**. O teste não abre Witcher,
DLSS ou Quest; recibos e olhos continuam fabricados. Nada foi instalado.
5.00c continua bloqueado e o mod não está jogável. Evidência exclusivamente
local: `../artifacts/modern-dlss-native-hooks-port-validation.json`.

## Retenção das texturas até o término da GPU — 6 de outubro

Uma textura é um objeto que guarda pixels. Mantê-la viva evita que o jogo
destrua esse objeto enquanto a placa de vídeo ainda precisa dele. O novo
`modern_dlss_retirement` segue este fluxo:

1. Guarda a gravação antes do envio e confirma a fila exata, a placa e a
   presença da lista no conjunto de tarefas que o chamador pretende enviar.
2. Antes do envio, marca que a GPU poderá usar os recursos. Depois do envio,
   pede uma confirmação numerada própria dessa fila, chamada fence.
3. Só libera o envio quando essa confirmação termina. Uma confirmação de
   outra fila não serve. Se o pedido falhar, conserva tudo e bloqueia novos
   envios acompanhados; uma tentativa posterior usa outro número.
4. Conserva a gravação mesmo após a conclusão, porque a mesma lista fechada
   pode ser reenviada. Um Reset real bem-sucedido encerra essa possibilidade;
   envios antigos ainda pendentes conservam suas próprias referências.
5. Se o componente for destruído antes disso, transfere o estado já alocado
   para uma área de retenção. Falha sem recuperação ou remoção do dispositivo
   não libera referências por suposição; pode retê-las até o processo terminar.

Escolhi confirmações próprias para impedir que números iguais de filas
diferentes sejam confundidos. A retenção não congela os pixels nem comprova
uma imagem correta de VR. O futuro adaptador do jogo precisa acompanhar as
operações reais na ordem correta; este componente ainda não está ligado aos
pontos de envio do Witcher e não executa nem altera a lista enviada.

A DLL compilou e passaram **107/107 testes em 12,38 segundos**. O teste
independente na RTX 4070 Ti passou **26 cópias reais**: 24 gravações, um
reenvio e uma cópia que sobreviveu ao encerramento do componente. Recusou
fila diferente da mesma placa, confirmação externa, gravação antiga e
reservas acima do limite. Uma falha de Signal foi **simulada**; a nova
tentativa, as esperas, as confirmações e a conferência dos pixels foram reais.
Os recibos e olhos são fabricados para o teste: não há chamada real ao DLSS,
execução no jogo ou teste no Quest. Nada foi instalado; o bloqueio de 5.00c
continua fechado. Evidência local: `../artifacts/modern-dlss-retirement-port-validation.json`.

## Proteção contra reaproveitamento de gravações — 6 de outubro

Uma lista de comandos é a sequência de tarefas enviada à placa de vídeo.
O jogo pode reutilizar a mesma lista para outro quadro. Acrescentei uma
identificação própria do objeto e um número que muda quando começa uma
gravação nova. O código faz o seguinte:

1. Confirma que é a mesma lista e o mesmo dispositivo da aquisição de texturas.
2. Só inicia um número novo após o retorno bem-sucedido de Reset, a operação
   que reabre a lista para gravar. Falha não cria uma gravação válida.
3. Compara o número capturado antes de produzir a imagem com o número atual;
   recusa dados antigos, repetidos ou provenientes de outra lista.
4. Depois de Close, a operação que encerra a gravação, entrega as referências
   das texturas uma única vez. Uma gravação posterior não apaga esse pacote.

O teste independente na RTX 4070 Ti passou 24 cópias reais: bloqueou
temporariamente a fila, reutilizou a lista com outro espaço de gravação
enquanto o trabalho anterior estava pendente e conferiu os pixels depois
da confirmação da GPU. Também recusou falhas reais de Reset/Close.
As associações de olho e quadro nesse teste são fabricadas; ele não carrega
o jogo, Streamline ou o headset. DLL compilada; 107/107 testes em 11,81s.

O componente novo ainda não está ligado aos observadores do jogo. Falta verificar todos
os caminhos reais de Reset/Close/Execute e guardar os pacotes na fila correta
até seu término, inclusive em falhas. Nenhuma proteção aqui autoriza liberar
texturas em uso ou chamar um pacote fechado de imagem pronta para VR.
Relatório local: `../artifacts/modern-dlss-recording-port-validation.json`.

## Identificação da fila da placa de vídeo — 6 de outubro

Acrescentei uma verificação da fila que recebe os comandos. Ela guarda uma
referência própria da fila, confirma o dispositivo a que ela pertence e
recusa tipos de fila que esse caminho ainda não suporta. Duas filas da mesma
placa mantêm identificações diferentes: uma confirmação de término de uma
delas não pode ser aproveitada automaticamente pela outra.

O teste na RTX 4070 Ti passou com filas reais: identificações distintas,
recusa de fila de outro dispositivo, recusa do tipo computação e 24 cópias
de textura feitas usando a referência adquirida da fila. Os 107 testes
passaram em 11,75s e a DLL compilou. A identificação da fila da biblioteca
Streamline instalada foi conferida por análise estática, ou seja, leitura
do arquivo sem executá-lo. Esse caminho Streamline ainda não foi testado
com o jogo aberto. Não houve teste no Quest nem medição em Novigrad.

Falta conectar essas referências ao envio real de cada pacote, guardar sua
confirmação de término específica e tratar falhas e encerramentos. O jogo
continua sem receber esta DLL. Evidências novas ficam somente locais em
`../artifacts/modern-dlss-queue-port-validation.json`.

## O que foi verificado

O ponto de partida é [Witcher3VR](https://github.com/tig3rmast3r/witcher3-vr),
do tig3rmast3r, versão v0.9.8v2, commit
`79293dd5f523af11579e061aa0b777d65225911c`. O autor informa que o projeto não
oferece controles de movimento. A cópia local foi separada na linha de trabalho
`quest3-motion-controls`. O fork foi criado na conta
[pauloenrico8-jpg/witcher3-vr](https://github.com/pauloenrico8-jpg/witcher3-vr).
O código de leitura dos controles, os testes simulados e as ferramentas de
análise e instalação foram publicados na
[linha quest3-motion-controls](https://github.com/pauloenrico8-jpg/witcher3-vr/tree/quest3-motion-controls).

Seu jogo Steam está instalado em
`E:\SteamLibrary\steamapps\common\The Witcher 3`. O executável DX12 informa
`5.0.0.1044392(Build Machine)`, com SHA-256
`9406ECCC12B68E08920931442EF6A57340E910D3E01F2082E88232487433FE51`.
A atualização pública consultada é o
[hotfix 5.00c](https://www.thewitcher.com/us/en/news/52073/hotfix-5-00c-out-now-for-the-witcher-3-wild-hunt-remastered-on-pc).

O computador tem i5-14600KF, RTX 4070 Ti e aproximadamente 32 GB de RAM.
Esses dados identificam a máquina; não demonstram que ela atingirá a meta em VR.
O caminho escolhido pelo usuário é Quest 3 com Virtual Desktop. O OpenXR ativo
no Windows, no momento da inspeção, era o SteamVR; não foi alterado.

## Por que a versão atual ainda não abre como VR

O código base acessa partes internas do jogo usando endereços da versão 4.04.
Nos dois endereços usados para acompanhar a cabeça do personagem, os bytes do
executável instalado não correspondem ao esperado. Há vários outros endereços
fixos no mesmo código. A discussão sobre suporte ao Remastered está na
[issue 27](https://github.com/tig3rmast3r/witcher3-vr/issues/27).

Uma busca no executável encontrou os mesmos inícios de duas funções em outros
endereços: `0x02102690` e `0x02251A40`. A análise das instruções confirmou que
esses endereços são registrados junto aos nomes `GetHeadBoneIndex` e
`GetBoneWorldMatrixByIndex`. São evidências para a adaptação.
Isso não confirma o funcionamento completo dessas funções, seus parâmetros,
as estruturas do personagem ou os demais recursos do motor. Nenhum endereço
foi substituído por tentativa.

O protótipo inclui uma verificação anterior à ativação dos recursos: se os dois
pontos conhecidos não corresponderem, os recursos do mod ficam inativos.
Essa verificação evita tentar usar o código antigo no executável instalado.
Ela não adapta o mod ao Remastered. Mesmo uma correspondência futura não
comprova, sozinha, a compatibilidade de todos os recursos.

Evidências locais: [checagem dos endereços](diagnostics/game-compatibility.json)
e [candidatos encontrados](diagnostics/legacy-pattern-candidates.json), além da
[análise dos registros de funções](diagnostics/remastered-callback-evidence.json).

O programa `scripts/analyze-remastered-callbacks.py` lê o executável sem
executá-lo ou alterá-lo. Primeiro identifica suas regiões de código; depois
procura os nomes das funções e confere as instruções que apontam para eles.
Por fim, grava um relatório para comparar as evidências. Foi usado Capstone
5.0.6, uma biblioteca que traduz instruções do processador para texto legível.
O relatório ajuda a evitar a escolha de um endereço apenas porque seus bytes
parecem semelhantes. Ele não libera o mod para executar no Remastered.
Também foram identificados registros para cenas, câmera manual, câmera ativa,
telas de carregamento, projeção e menus. `GetCameraDirection` aparece em duas
classes; o relatório conserva ambos os candidatos para análise, sem escolher
um deles automaticamente. Algumas funções pequenas não têm registro de
recuperação de pilha no executável; essa diferença também é anotada.

Além dessas funções, o código base depende de funções internas de renderização,
endereços de retorno, tabelas globais e posições de campos dentro dos objetos.
Esses pontos ainda precisam ser comparados. Substituir apenas os endereços da
câmera não seria uma adaptação completa e poderia corromper a memória do jogo.
O [inventário dos pontos antigos](diagnostics/remastered-port-inventory.json)
registra 41 constantes de endereço e 17 entradas em duas listas de endereços.
Onze constantes têm candidatos identificados pelos registros de funções.
Ainda não existe verificação em execução para qualquer um desses candidatos.
Esse inventário não inclui todos os campos de objetos ou endereços escritos
diretamente em outras expressões; não é uma porcentagem de conclusão do port.

A análise de 06/10 acrescentou a identificação das classes pelo compilador e
o tratamento dos blocos separados de uma função. Ela distingue o callback
`GetCameraDirection` do diretor de câmera daquele da classe `CCamera`, encontrou
uma cadeia de execução de renderização e confirmou acessos incompatíveis entre
os dados de câmera das versões. As evidências e o fluxo da ferramenta estão em
[PORT-REMASTERED.md](PORT-REMASTERED.md). São 15 tabelas de classes, 43 referências
a tabelas e 145 entradas em um grafo limitado, sem ativação em execução.

## Adaptação do histórico de câmera por versão

O histórico da câmera guarda como ela estava na imagem anterior. O jogo usa
isso para calcular movimentos na imagem. A parte que constrói e grava esse
histórico passou para `src/engine_camera_temporal.h`, com uma configuração
própria para 4.04 e outra examinada para 5.00c.

1. Lê só os dados de entrada necessários: posição, direção, tempo e projeção.
   Recusa dados incompletos, números inválidos e projeções degeneradas.
2. Pede à função original do jogo para construir o registro numa área temporária.
   Mantém a ordem dos argumentos e o alinhamento que essas funções podem exigir.
3. Confere se a função devolveu o registro esperado e o marcou como válido.
   Um resultado inválido não substitui o histórico anterior.
4. Grava somente o trecho destinado a esse histórico na versão escolhida.
   Os dados das versões ficam em posições diferentes dentro da câmera; escrever
   na posição antiga no Remastered alteraria outro trecho.
5. Atualiza o histórico daquele olho apenas depois da escrita bem-sucedida.

Escolhi essa divisão porque mudar somente o endereço de uma função deixa
os acessos antigos aos dados dentro do objeto. Os dois caminhos de histórico
na DLL agora usam esse módulo, e três endereços de funções de câmera vêm da
mesma configuração. A configuração do 5.00c permanece sem ativação: outras
partes da renderização ainda precisam de adaptação. O bloqueio anterior continua.

O teste novo simula a função do jogo. Confere os argumentos e os limites de
memória, incluindo que a configuração 5.00c conserva intacta a área usada pelo
histórico antigo. **Esse teste não confirma execução dentro do jogo.**
Os relatórios novos dessa rodada de câmera permanecem no projeto local.

A DLL compilou após a revisão final, e a suíte do PC passou em **98/98 testes**.
O registro desta rodada está em
`../artifacts/test-camera-contract-final-20261006.log`. São testes de código e
contratos simulados; não são uma abertura do jogo nem uma medição de VR.

## Envio da cena e duração dos dados

Uma cena precisa continuar disponível enquanto a placa de vídeo e o jogo a
processam. A parte nova em `src/engine_frame_submission.h` prepara o envio
usado no Remastered, com este fluxo:

1. Confere as funções e o renderizador antes de começar.
2. Pede ao jogo uma área própria para seu comando de renderização. Recusa
   falhas e um objeto especial que o jogo não despacha.
3. Coloca a cena nesse comando. O jogo mantém uma referência adicional à cena:
   é um registro que impede que ela seja destruída enquanto o comando a usa.
4. Envia o comando e libera somente a referência criada pela nossa chamada
   à fábrica de cenas. O comando libera sua própria referência ao terminar.
5. Se falhar antes do envio, libera os dados que pertencem à nossa chamada
   e retira o par incompleto da identificação dos olhos.

Essa divisão foi necessária porque o envio no 5.00c usa objetos e dados de fila
que diferem da passagem antiga. Uma área criada pelo próprio mod não contém
os dados internos que o jogo exige para esses comandos.

O envio do olho duplicado agora passa por essa escolha de rota. **A rota nova
do 5.00c continua sem ativação**, junto do bloqueio geral. Ainda faltam o
descritor completo da cena, a preparação do produtor e outros campos e chamadas.
Os testes de duração dos dados são simulados: nenhum frame do jogo foi enviado
pelo novo código nesta rodada. Seu relatório novo permanece somente local.

A DLL desta rodada compilou e a suíte completa passou em **99/99 testes**.
Registro: `../artifacts/test-frame-submission-20261006.log`. Esse resultado
verifica o código no PC, incluindo as funções simuladas; não demonstra VR
jogável nem desempenho no save de Novigrad.

## Condições para criar uma cena

O código novo de `src/engine_scene_factory.h` corrige uma diferença nos
argumentos usados pela fábrica de cenas. A fábrica é a função do jogo que
monta os dados de uma imagem. No 5.00c, um argumento que o mod antigo exigia
vem vazio; as configurações estão dentro dos dados da cena.

Agora o código escolhe a condição da versão: mantém a regra antiga em 4.04;
para 5.00c, confere a presença do renderizador e da cena, lê largura/altura e
recusa dimensões inválidas. Quatro decisões de início ou identificação da
cena usam essa verificação. O endereço da fábrica também vem da versão
escolhida. Isso prepara a passagem para aceitar a chamada real do Remastered.

**5.00c continua sem ativação.** A cópia completa dos dados da cena ainda
precisa de revisão, incluindo os objetos que esses dados apontam. Uma segunda
construção de frame foi identificada, mas ela não basta para confirmar o
tamanho e a duração de todos esses dados. Seu relatório novo permanece local.

A DLL compilou e a suíte completa passou em **100/100 testes do PC**.
Registro: `../artifacts/test-factory-admission-20261006.log`. O teste novo
confere as condições por versão com dados simulados; não executa a fábrica
real do jogo nem comprova imagens no headset.

## Cópia dos dados de cena na versão nova

O descritor é o bloco de informações que a fábrica usa para montar uma imagem.
O Remastered usa dados além do bloco copiado pelo mod antigo. O novo módulo
`src/engine_scene_descriptor.h` separa as regras de cópia por versão.

1. Para 4.04, conserva os tamanhos e o preenchimento usados pelo projeto base.
2. Para 5.00c, pede os `0xF750` bytes da região observada nos dois construtores
   de imagem. Esse tamanho foi confrontado com as rotinas de cópia, incluindo
   suas partes menores. Uma cópia cortada não é aceita nem completada com zeros.
3. Confere que uma lista interna cabe antes do próximo campo fixo: aceita de
   zero a quatro elementos. Também exige o alinhamento de memória esperado.
   Alinhamento é a posição do bloco em múltiplos de um tamanho exigido pelo jogo.
4. Guarda a cópia nova somente durante a chamada que cria a imagem. Ela contém
   endereços emprestados do jogo; copiá-los não dá ao mod a propriedade dos
   objetos apontados. A fábrica nativa deve criar suas próprias cópias e
   referências antes de retornar. A liberação do bloco do mod apaga apenas os
   seus bytes, sem destruir os objetos do jogo.

A ferramenta de análise também passou a examinar rotinas pequenas sem índice
de função, mas somente quando uma chamada real já identificou sua entrada.
Ela para nos retornos, limita o percurso e recusa instruções incompatíveis com
esse tipo de rotina. Isso permitiu revisar 28 chamadas que antes ficavam sem
corpo analisado. Os detalhes estão em [PORT-REMASTERED.md](PORT-REMASTERED.md).

A DLL compilou e **101/101 testes C++ e 37 testes de análise passaram**.
Os casos novos conferem cópias cortadas, listas que ultrapassariam o bloco,
alinhamento e chamadas falsas dentro de dados. Registros:
`../artifacts/test-descriptor-final-20261006.log` e
`../artifacts/test-leaf-analysis-20261006.log`.

**O caminho 5.00c continua bloqueado e a DLL não foi instalada.** Esse contrato
é uma etapa da adaptação: a duração dos recursos ainda precisa de teste nativo,
e faltam preparação do produtor, campos de câmera e constantes de renderização.
Não houve criação de imagens no jogo, combate físico ou medição no Quest.
Os relatórios novos permanecem locais.

## O que o código novo faz, passo a passo

1. Quando o mod base cria sua ligação com o sistema de VR, a parte nova pede
   acesso aos controles esquerdo e direito. OpenXR é a interface usada para
   conversar com o sistema de VR do PC.
2. A cada imagem, ela consulta a posição, a direção e, quando disponíveis, as
   velocidades de cada controle. Também lê gatilhos, botões e direcionais.
3. Ela mantém as duas mãos separadas. Se perder o acompanhamento de uma mão ou
   se o jogo deixar de receber os controles, descarta os dados antigos.
4. Ela guarda pares de posições consecutivas de cada mão, com o intervalo de
   tempo. Descarta a continuidade quando perde rastreamento/foco, muda a sessão
   ou a origem de referência, recebe tempo repetido/invertido ou passa mais de
   100 ms entre amostras. Ainda falta
   usar os dados para desenhar as mãos, mover as armas e calcular contato,
   dano e bloqueio pela trajetória real da espada.
5. Existe uma função para solicitar vibração curta nos controles. Nenhuma
   colisão do jogo a utiliza ainda.

Foi escolhida a leitura das posições reais porque o pedido é de combate pela
trajetória da mão. O código novo não transforma um movimento em um botão de
ataque do jogo. Também não contém ainda o combate físico solicitado.

O arquivo `src/sword_sweep.h` acrescenta uma parte matemática para estudar
contato da lâmina. Ainda não está ligado aos controles ou aos inimigos do jogo.
Seu fluxo é:

1. Recebe as duas pontas da lâmina e do alvo na amostra anterior e na atual,
   com espessuras em metros e o intervalo de tempo entre as amostras.
2. Confere números, medidas e intervalo; descarta dados inválidos ou muito antigos.
3. Calcula a menor distância entre lâmina e alvo. Depois avança no intervalo
   em passos limitados pela distância que as pontas podem percorrer.
4. Se as formas se encostarem, informa o momento aproximado, os pontos mais
   próximos e a velocidade relativa. Isso é apenas um candidato a contato.
5. Se o limite de cálculo acabar, informa que o resultado é inconclusivo;
   não inventa contato nem afirma que houve uma passagem sem contato.

Essa abordagem foi escolhida porque uma espada rápida pode atravessar um alvo
fino entre duas imagens: olhar apenas as posições medidas perderia esse contato.
Esse primeiro modelo supõe trajetórias retas para as pontas entre as amostras.
O novo `src/rigid_sword_sweep.h` trata posição e rotação de uma lâmina rígida:
move a mão em linha reta e gira a lâmina pelo menor arco entre as orientações
medidas, mantendo seu comprimento. Confere contato ao longo desse arco e calcula
a velocidade no ponto de contato, incluindo a rotação. É uma aproximação entre
amostras: não descobre voltas inteiras ou mudanças de direção que o rastreamento
não mediu. O alvo ainda usa trajetórias retas para suas pontas.

`src/hand_pose_history.h` está ligado à leitura dos controles na DLL. Guarda
posição anterior/atual em metros, direção e tempo para as duas mãos. A mudança
de origem segue o horário informado pelo
[evento do OpenXR](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XrEventDataReferenceSpaceChangePending.html),
inclusive quando o renderizador consulta tempos fora de ordem. O histórico
da sessão é usado para impedir que uma mudança de referência pareça um golpe.
A função de contato por arco está nos testes; ainda não é chamada contra alvos
do jogo. Antes disso, falta converter as coordenadas para o Witcher, reiniciar
o histórico em mudanças de arma/âncora ou teleporte, identificar alvos reais,
controlar repetição de
contatos e ligar os resultados às regras de dano e bloqueio do Witcher.
Não existe emissão de botões, dano, animação ou vibração a partir desse cálculo.

A configuração de exemplo solicita o modo do projeto base que produz imagens
para ambos os olhos, sem alternância entre olhos. É uma configuração para
desenvolvimento, não um perfil de desempenho validado. A leitura dos controles
fica desligada por padrão e pode ser solicitada em `[motion_controllers]`.
O bloqueio de incompatibilidade continua ativo com qualquer configuração.

## O que os testes comprovam

A compilação no Visual Studio 2026 terminou. Os 97 testes de software passaram
na rodada que acrescentou arcos e histórico de mãos; o registro está em
`../artifacts/test-quest3-rigid-20261006.log`.
O teste novo de controles usa um sistema de VR simulado: verifica mãos
independentes, leitura de botões, perda de acompanhamento, dados inválidos,
limites de vibração e encerramento dos recursos. Outro teste verifica a recusa
de imagens de programa alteradas ou truncadas.

O teste matemático da lâmina verifica passagem por alvos finos, contato entre
espadas, alvos em movimento, contatos já existentes, situações próximas sem
contato, pontas de espada, segmentos quase paralelos e dados inválidos.
Também compara os tempos calculados com soluções conhecidas e confere que
trocar a ordem das pontas ou deslocar todas as formas preserva o resultado.

Os testes de arco comparam tempos de contato com soluções conhecidas, verificam
alvos sobre o arco e fora dele, velocidade de rotação, comprimento da lâmina,
sinais equivalentes da orientação e limite de cálculo. O histórico é testado
com perda/recuperação de uma mão, falha de consulta, perda de foco, nova sessão,
mudança de origem no tempo correto, tempo duplicado/invertido e pausas longas.
Esses casos são simulados no PC; não aplicam dano ou bloqueio no jogo.

Três testes antigos do launcher falharam ao substituir seus arquivos temporários
fora da pasta de trabalho no ambiente restrito. Foram repetidos com `TEMP` e
`TMP` apontando para `build/quest3/test-temp`, dentro do projeto; todos passaram.
Essa alteração valeu apenas para os processos de teste e não mudou a instalação
do jogo nem as configurações do Windows. A suíte completa foi repetida nesse
ambiente para registrar o resultado de 95/95.

Separadamente, os 25 testes Python da análise do Remastered passaram em 06/10.
Eles conferem blocos encadeados, classes, arquivos truncados, gravações de
ponteiros globais e falsas instruções em dados. A reanálise do executável
preservou os 15 registros de nome/callback
anteriores. Os módulos de arcos/continuidade e as ferramentas de análise foram
publicados até o commit `809f2a00fe3285bd7958aa7191943de58bc0b49a`. Naquela
verificação anterior às adaptações de câmera, as árvores completas
local/publicada eram idênticas. Essa referência é histórica. As comparações
posteriores são registradas em `CONTINUAR-QUEST3.md`, na pasta superior ao
repositório. Os relatórios novos de dados continuam somente no projeto local.

Esses testes não usam o Quest, não abrem The Witcher 3, não comprovam imagens
corretas no headset e não medem Novigrad. O registro completo está em
`build/quest3/Testing/Temporary/LastTest.log`.

## Limpeza da instalação parcial de primeira pessoa

Os restos do GFP foram retirados da instalação e guardados em:
`E:\GAMES\mod VR witcher 3\backups\gfp-partial-20261005-214409`.
Foram movidos 11 itens, contendo 18 arquivos e 83.680.651 bytes. Cada arquivo
foi conferido por SHA-256 antes e depois da movimentação. O arquivo
`manifest.json` registra todos os caminhos e as verificações.

Isso conclui a retirada dos restos identificados.
Os saves, as configurações pessoais e os carregadores compartilhados ficaram
intactos. O GFP antigo não foi reinstalado no Remastered.

Foi instalado o pacote original anunciado para 5.0 DX12:
[Remastered First Person View Toggle](https://www.nexusmods.com/witcher3/mods/13072),
versão 2.0, baixado em 5 de outubro. O ZIP tem SHA-256
`1FFC8745C0CA4B116BC5500368142B680427E96A7B9158BE4F8AC3DC35C8E3D9`.
Foram copiados sete arquivos originais e configurados os atalhos F8 (alternar
primeira/terceira pessoa) e F9 (mira). Não foi adicionada a combinação com a roda
do mouse, evitando sobreposição aos controles já usados pelo jogo.
O carregador `dinput8.dll` e o ReShade `dxgi.dll` permaneceram iguais, conferidos
por SHA-256. Os saves não foram alterados. Os controles anteriores e o registro
da instalação estão em
`E:\GAMES\mod VR witcher 3\backups\fpv-toggle-2.0-20261005-2207\manifest.json`.

O instalador próprio `scripts/install-fpv-original.ps1` faz, nesta ordem:
confere que o jogo está fechado e que o pacote está completo; compara os
controles novos com os antigos para impedir mudanças não relacionadas; guarda
as cópias de segurança; copia os arquivos; compara seus identificadores
de conteúdo e registra o resultado. Esses identificadores, chamados SHA-256,
permitem conferir que o arquivo copiado é exatamente igual ao original.
O instalador não contém nem publica os arquivos de terceiros.

Os arquivos estão instalados, mas ainda não houve teste de abertura ou
compilação dos scripts dentro do jogo. A opção de habilitar mods no REDlauncher
também não foi confirmada pela interface. O autor recomenda o
[Snappy Stop](https://www.nexusmods.com/witcher3/mods/13592); esse complemento
ainda não foi instalado. Sua integração com este projeto não foi testada.
É um mod de câmera de primeira
pessoa, sem o sistema de mãos e combate físico solicitado. Sua licença também
exige autorização do autor para modificar ou incorporar seus arquivos; o
projeto local não contém esses arquivos.

## Quando o Quest será necessário

Agora é possível continuar a análise de compatibilidade e desenvolver o código
no PC. A falta de bateria não impede essas etapas.

Depois de existir uma versão que abra no jogo, será necessário conectar o Quest
para conferir profundidade nos dois olhos, movimentos da cabeça, posição das
mãos, espada, contato, bloqueios e transmissão pelo Virtual Desktop.
A antiga meta de desempenho em Novigrad foi cancelada pelo usuário em 06/10.
A validação no aparelho continua necessária para confirmar que o VR funciona.

## Preparação da segunda imagem no Remastered

O código agora guarda a imagem adicional até a cena normal chegar ao ponto
de envio do jogo. Essa etapa está em `src/engine_frame_preparation.h` e ligada
ao produtor de cenas na DLL. O fluxo preparado para 5.00c é:

1. A função original recebe o objeto do jogo e cria as duas imagens. A cópia
   adicional mantém sua própria referência: o jogo não pode liberá-la antes
   de o mod terminar de usá-la.
2. O mod deixa a preparação normal terminar e observa quais funções realmente
   receberam a imagem principal. Guarda seus argumentos, sem procurar o
   objeto do jogo em um endereço global presumido.
3. O jogo avança o tempo dos efeitos uma vez. O mod registra essa passagem;
   não repete o relógio para o segundo olho.
4. No ponto em que o jogo cria o comando de envio, o mod prepara a imagem
   adicional e aplica os efeitos observados, usando as funções originais.
   Se os efeitos não foram usados na cena principal, não são acrescentados.
5. O comando mantém uma referência própria à imagem adicional. O mod entrega
   sua referência após o envio. Se a cena parar antes desse ponto, libera a
   imagem e remove a identificação do par incompleto.

Cada execução do produtor guarda seu próprio estado. Chamadas aninhadas e
execuções em outras threads (linhas de trabalho do processador) não dividem
a mesma imagem pendente. Os testes também verificam uma chamada de volta
durante a preparação, evitando enviar a mesma imagem duas vezes.

A preparação tem condições estreitas: o callback opcional do mundo precisa
ser ausente ou a função vazia examinada; um callback adicional em Engine+40
precisa estar ausente. O papel desse último callback ainda não foi confirmado.
Mudanças nos objetos do mundo, efeitos ou motor durante a preparação também
cancelam o par. Esses casos precisam de adaptação adicional.

**O Remastered continua sem ativação.** Os efeitos escrevem alguns caches
compartilhados, além dos dados da imagem. A aplicação repetida, os recursos
nativos e a duração de seus objetos ainda não foram testados no jogo. Essa
implementação não libera o restante dos endereços e campos antigos.

A DLL compilou e a suíte de 102 testes do PC passou; a nova verificação simula ordem,
relógio, cancelamento, referências, chamadas aninhadas e simultâneas. Esses
testes não abrem o jogo nem demonstram VR ou 60 FPS. Evidência nativa local:
`../artifacts/remastered-preparation-evidence.json`. A comparação completa do
fork e o próximo passo ficam registrados em `../CONTINUAR-QUEST3.md`.

## Campos de câmera por versão: etapa de 6 de outubro

O mapa de campos está em `src/engine_camera_layout.h`. Um campo é uma posição
dentro dos dados guardados pelo jogo: por exemplo, onde fica a largura da
imagem. A versão nova mudou essas posições de maneiras diferentes. Somar o
mesmo número a todas elas faria o mod ler ou escrever dados errados.

O código desta etapa funciona assim:

1. Usa a versão já aceita pela inicialização para escolher o mapa. Uma versão
   desconhecida não recebe automaticamente o mapa antigo.
2. Distingue o descritor da cena dos dados completos da imagem. O segundo
   contém o descritor em outra posição; por isso, suas câmeras têm endereços
   diferentes. A câmera secundária nova fica em frame+600, não frame+530.
   São dois registros internos de uma mesma cena. Eles não são as imagens
   esquerda e direita do Quest: essas ainda precisam de cenas renderizadas
   separadamente pelo mod.
3. Lê posição, orientação, campo de visão, pequenos deslocamentos de câmera
   e tamanho da imagem nos campos daquela versão. Esses deslocamentos,
   chamados jitter, ajudam algumas técnicas de suavização a combinar imagens.
   No Remastered, o local antigo do jitter agora contém parte de uma matriz,
   um conjunto de números usado para calcular a imagem.
4. Guarda o resultado somente se a leitura inteira estiver disponível. Dados
   incompletos não substituem uma leitura anterior válida. Os verificadores
   das duas câmeras e os registros da fábrica agora usam esse mesmo mapa.
5. Recusa instalar o escritor temporal antigo no Remastered. A função nova
   encontrada tem duas chamadas de supersampling — vários desenhos de uma
   cena para combinar amostras — e outra do desenho final 2D. Uma chamada
   restaura valores anteriores. Ela precisa ser tratada separadamente para
   não aplicar o deslocamento de um olho duas vezes.

A DLL compilou e os 103 testes do CTest passaram. O teste novo usa dados
montados no PC para conferir câmeras distintas, posições antigas que agora
contêm outros dados, limites de leitura, versão desconhecida e separação das
três rotas novas. Os testes não executam essas funções dentro do jogo.

Ainda faltam as rotas temporais da imagem normal, as constantes enviadas à
placa de vídeo e os outros pontos de câmera e jogo. A inicialização continua
aceitando somente 4.04; o mod não está funcionando no Remastered nem foi
instalado nesta etapa. Não houve teste no Quest ou medição de FPS.
Logs locais: `../artifacts/build-camera-layout-final-20261006.log` e
`../artifacts/test-camera-layout-final-20261006.log`. O relatório novo
`../artifacts/remastered-camera-layout-evidence.json` permanece local.

## Rota da câmera durante o desenho normal: 6 de outubro

Encontrei a chamada normal do ajuste de câmera. A ferramenta anterior não
seguia uma tabela de escolhas da versão nova e, por isso, tinha encontrado
apenas as chamadas de supersampling e desenho 2D. Esse limite foi corrigido;
os relatórios antigos continuam sendo evidência parcial.

O código novo funciona assim:

1. A ferramenta confere a comparação que limita as escolhas e os destinos
   da tabela antes de segui-los. Uma tabela truncada, um destino em outra
   função ou bytes confundidos com instruções são recusados.
2. O mod usa a versão aceita para escolher a função de ajuste da câmera e
   reconhecer quem a chamou. A atualização da cena normal recebe seu próprio
   tratamento. A restauração de valores e o desenho 2D não recebem novamente
   o deslocamento necessário para posicionar a imagem de um olho.
3. A conferência usa as posições novas do jitter atual, sem confundi-las com
   o jitter da imagem anterior. A dica de centralização da versão antiga não
   é reutilizada na nova. As capturas de auditoria antigas ficam na versão antiga.
4. O bloqueio de compatibilidade permanece antes da instalação dos hooks,
   as funções que permitem ao mod interceptar o trabalho do jogo. Ainda não
   foi liberada execução em Remastered 5.00c.

Também identifiquei a função nova que prepara as constantes, os números que
o jogo envia à placa de vídeo para desenhar a imagem. Ela mudou de endereço
e usa outra posição para guardar o identificador da imagem. Adaptar essa
função e os dados anteriores da câmera é o próximo passo.

A DLL foi compilada; 103/103 CTest e 43/43 testes Python passaram. As verificações finais desta rodada estão registradas
no relatório local `../artifacts/normal-camera-switch-port-validation.json`.
Nenhum teste desta etapa comprova VR no Quest, combate físico ou 60 FPS.
Os novos dados extraídos do executável continuam locais.

## Preparação dos dados para o DLSS: 6 de outubro

A versão instalada usa uma forma nova de chamar o Streamline, o componente
que recebe os dados usados pelo DLSS. A função antiga recebia números para
identificar a imagem e a área de desenho; a nova recebe referências a objetos.
O código agora separa essas duas formas de chamada.

O fluxo da adaptação é este:

1. A versão aceita escolhe o endereço da função e o lugar onde estão o número
   da imagem e o registro de que seus dados já foram preparados. Não existe
   uma soma única aplicada aos campos da versão antiga.
2. Na chamada normal nova, o mod acompanha o estado e o descritor realmente
   recebidos do jogo, apenas enquanto essa chamada está em execução. Ele não
   procura o estado novo usando a tabela global da versão antiga.
3. A adaptação do Streamline confere a identificação e a versão dos dados,
   lê o jitter (pequeno deslocamento usado para suavizar a imagem) e preserva
   os objetos originais. Encaminha a chamada e devolve sua resposta real.
4. O registro de preparo só é aceito depois de uma resposta de sucesso,
   para o mesmo par de imagens, olho e geração. A chamada antiga também foi
   corrigida para registrar sucesso apenas depois dessa resposta.
5. Repetir o preparo para o segundo olho no Remastered permanece desativado.
   Ainda falta adaptar, em conjunto, a identificação dos recursos de cada
   imagem, as áreas de desenho separadas e a execução do DLSS. Os campos e
   as chamadas da versão antiga não são usados como substitutos.

A DLL compilou e 104/104 CTest passaram (14,31 segundos). O teste novo usa
memória controlada do PC para conferir campos por versão, leitura incompleta,
identificação errada, sinalizador inválido e recusas de registros de sucesso.
Isso não executa o jogo nem comprova imagens, controles ou FPS no Quest.
A evidência nova permanece local em
`../artifacts/remastered-view-constants-abi-evidence.json`; nenhum dado novo
extraído do executável foi autorizado para publicação. A instalação, os saves,
os backups e os arquivos de terceiros foram preservados. O bloqueio global
para 5.00c permanece.

## Chamadas modernas de recursos e DLSS: 6 de outubro

Adaptei a forma nova de encaminhar os recursos de imagem e a execução do DLSS.
Essas chamadas agora usam cinco argumentos, incluindo a lista de comandos da
placa de vídeo. Usar a forma antiga trocaria os argumentos e poderia corromper
o trabalho do jogo. Os novos pontos continuam dentro do bloqueio global de
compatibilidade; não estão instalados no jogo.

O fluxo do código é este:

1. O contrato de dados separa o contador de renderização do índice usado para
   obter o identificador do quadro. Na versão nova, são dois campos vizinhos,
   com funções diferentes. O código os lê sem alterar os registros do jogo.
2. A identificação da área de desenho é conferida antes de interpretar seus
   dados. Objetos desconhecidos continuam sendo encaminhados ao componente
   original, mas não valem como prova de que a adaptação os reconheceu.
3. As chamadas preservam o identificador completo do quadro, os recursos,
   a lista inteira de entradas, a lista de comandos e a resposta original.
   Um erro continua sendo um erro; nenhum sucesso é inventado.
4. Os observadores distinguem DLSS comum e reconstrução de raios. Eles apenas
   registram chamadas reconhecidas; isso não prova uma imagem pronta na GPU.
5. A instalação futura exige os três pontos modernos em conjunto. Se falhar,
   tenta retirar o que criou e mantém os registros de sucesso desativados.
   Nunca troca os números das áreas de desenho para separar olhos nesta etapa.

A DLL compilou e 105/105 CTest passaram (11,68 segundos). O teste novo usa
endereços fictícios de 64 bits que não podem ser lidos para conferir que os
identificadores opacos não são truncados. Confere também vários recursos,
argumentos opcionais, respostas de erro, campos vizinhos distintos e leituras
incompletas. Esses testes não executam o jogo nem o headset.

A evidência local mostrou as chamadas do Streamline em funções diferentes da
candidata de pipeline anteriormente investigada. Essa candidata não foi
promovida a função de DLSS. Os novos relatórios permanecem fora do fork, em
`../artifacts/remastered-modern-*.json`. Próximo passo: conferir a obtenção das
opções do DLSS e a origem do estado de avaliação, antes de separar as áreas
e os históricos dos olhos ou repetir o preparo de um quadro.

## Entrada real e opções do DLSS: 6 de outubro, continuação

A análise encontrou a entrada real do DLSS no Remastered. A antiga candidata
continua sem esse papel confirmado. Adaptei a entrada encontrada, os pontos
de preparo e avaliação e o acesso à função que recebe as opções do DLSS.
Isso prepara a separação das imagens dos olhos, mas ainda não a executa.

O código trabalha assim:

1. A ferramenta de análise segue duas tabelas de nove modos. Ela confere o
   endereço usado pelas tabelas mesmo quando foi calculado antes delas.
   Se um caminho conhecido alterar esse endereço, recusa a interpretação.
2. A entrada do DLSS encaminha seus seis argumentos e conserva a resposta
   original de um byte. Os números de recursos passam intactos. A forma
   antiga de sete argumentos continua restrita à versão antiga do jogo.
3. Durante uma chamada reconhecida, o código acompanha o par de imagens,
   o olho, o quadro e o descritor, que é o pacote de dados da imagem. Usa
   o estado e a lista de recursos que o próprio jogo entrega aos métodos.
4. Confere que preparo, avaliação e chamadas do componente de imagem usam
   os mesmos objetos e a mesma área de desenho. Uma chamada aninhada ou
   desconhecida não herda essa associação. Uma diferença recusa a prova.
5. A função de opções é obtida pelo componente original. O código conserva
   o endereço devolvido e acompanha a função nesse endereço, inclusive se
   o jogo já o havia guardado. As opções, a espera ao trocar configurações
   e as respostas originais continuam intactas. Um erro não vira sucesso.
6. Nenhum identificador de área é trocado e nenhum preparo é repetido nesta
   etapa. Ainda é preciso verificar recursos, históricos independentes e
   sua liberação antes de ativar essas mudanças para os dois olhos.

A DLL compilou. Passaram 105/105 CTest em 11,71s e 50/50 testes Python.
Os testes verificam argumentos na pilha, respostas de um byte e de 64 bits,
trocas indevidas de quadro/olho/objeto, identificação de opções e preservação
dos dados. Isso não executa o jogo nem comprova VR, combate ou FPS.

As evidências novas estão locais em `../artifacts/remastered-modern-dlss-pipeline-owner-evidence.json`
e `remastered-modern-dlss-pipeline-recheck.json`. A segunda conferiu novamente
as duas tabelas completas da entrada, as quatro chamadas internas e seu
chamador conhecido. Um salto do chamador continua sem resolução; isso não
é apresentado como uma análise completa de todos os caminhos do jogo.
Nenhum relatório nativo novo foi publicado. Nada foi instalado no jogo.
O bloqueio global do Remastered 5.00c e o bloqueio de repetição permanecem.

## Texturas reais do DLSS: 6 de outubro, continuação

Adaptei a leitura das quatro texturas que o DLSS recebe no Remastered 5.00c.
Uma textura é uma imagem guardada na memória da placa de vídeo. Neste caso,
elas representam profundidade, movimento dos objetos, cor antes da melhoria
de imagem e cor depois dela. Identificar as quatro permite preparar históricos
separados para os olhos sem interpretar esses dados com o formato antigo.

O código funciona nesta ordem:

1. Confere que o estado recebido é o mesmo criado pelo jogo. A tabela de
   funções do renderizador, o acesso ao estado e o indicador de inicialização
   do componente precisam corresponder aos pontos examinados no executável.
2. Lê o cabeçalho de cada pacote e recusa um formato, versão ou extensão que
   não reconhece. Copia os endereços e tamanhos enquanto a chamada original
   ainda tem os dados disponíveis. Não modifica os pacotes do jogo.
3. Começa uma lista vazia a cada avaliação reconhecida. Reúne profundidade,
   movimento, entrada e saída somente para o mesmo olho, quadro, estado,
   identificador de área e lista de comandos da placa de vídeo.
4. Recusa a lista se faltar uma textura, houver remoção, repetição, erro,
   mudança de olho/quadro ou uso de uma mesma textura em dois desses papéis.
   Uma chamada intermediária ou aninhada também impede aproveitar a prova.
5. Confere os tamanhos usados pelo jogo e associa a resposta original do
   DLSS à lista. Consome a lista depois da tentativa; uma segunda avaliação
   não aproveita dados antigos. O identificador nativo ímpar fica intacto.
6. Registra apenas o que foi observado durante as chamadas no processador.
   Endereços copiados não dão ao mod a posse das texturas. Ainda falta
   acompanhar a lista de comandos até sua execução e término na placa de
   vídeo antes de guardar, substituir ou liberar históricos de cada olho.

Escolhi essas verificações para impedir que a imagem de um olho receba
profundidade, movimento ou histórico do outro. Não criei novos identificadores
nem ativei repetição de renderização com essas observações incompletas.

A DLL compilou e passaram **106/106 CTest em 11,72s**. Os testes novos exercitam
trocas de olho/quadro/estado, remoção, formatos desconhecidos, texturas repetidas,
falhas do componente e atividade intermediária. São testes do código fora do
jogo; não comprovam imagens no Quest, combate físico nem os 60 FPS.

Evidência nova exclusivamente local:
`../artifacts/remastered-modern-dlss-resource-layout-evidence.json`.
Nenhum relatório novo, binário de terceiro ou save foi publicado. Nenhum
arquivo foi instalado no jogo; o bloqueio do Remastered e da repetição moderna
continua. O próximo passo é verificar posse e tempo de uso dessas texturas,
identidade da lista de comandos após reutilização e conclusão na placa de vídeo,
além de tratar reinicialização/desligamento do componente antes de separar os
históricos. Os demais requisitos do mod permanecem pendentes.

## Posse das texturas e lista real de comandos: 6 de outubro, continuação

Criei uma parte que consegue manter objetos reais da placa de vídeo vivos
enquanto o código precisa deles. Antes, havia somente endereços copiados.
Uma referência COM é uma reserva de uso: impede que o objeto seja destruído
até que essa reserva seja devolvida. Isso não congela o conteúdo da imagem.

O fluxo é este:

1. Recebe a lista de quatro texturas já conferida na etapa anterior.
2. Reconhece o dono da lista de comandos. Um objeto desconhecido não é
   aceito como uma lista real somente porque foi devolvido sem erro.
3. Para a lista intermediária da NVIDIA, confere a tabela de funções,
   os identificadores e a impressão digital da função instalada. Somente
   a passagem para a lista original que foi examinada é permitida.
4. Pede uma referência real da lista, do dispositivo e de cada textura.
   Confere que todos pertencem ao mesmo dispositivo e que as quatro
   texturas são objetos diferentes, mesmo se seus endereços aparentarem
   representar interfaces diferentes do mesmo objeto.
5. Confere formato de imagem, tamanho, ausência de múltiplas amostras
   e a possibilidade de escrever na textura de saída. Uma falha devolve
   todas as referências adquiridas, sem deixar uma lista parcial válida.
6. As referências permanecem com o resultado enquanto ele existir e são
   devolvidas automaticamente quando termina seu uso. No observador futuro
   do jogo, são temporárias: ainda não ficam guardadas até o término na GPU.
7. Uma função separada verifica o número de conclusão da GPU. Recusa zero
   como destino e o valor especial que indica perda do dispositivo.

Escolhi essa abordagem para impedir o uso de objetos já destruídos ou de
outra placa. Não executei nem modifiquei comandos do jogo. A chamada geral
do SDK da NVIDIA continua bloqueada sem inicialização e sincronização
comprovadas; a passagem de interface conferida não precisa dessa chamada.

A prova em `../artifacts/remastered-modern-streamline-command-base-evidence.json`
confirmou a passagem de interface no componente instalado. A busca anterior
apenas por instruções de endereço era parcial: as duas comparações do
identificador estão na própria função. Essa análise não executa o componente.

O teste independente usa texturas e comandos reais da **RTX 4070 Ti**.
Passaram 24 cópias de imagem com espera de conclusão e conferência dos bytes
devolvidos. Ele soltou as referências do chamador para verificar que as
referências adquiridas mantêm as imagens vivas. Também recusou repetição,
imagem pequena, saída sem escrita, um buffer e uma textura de outro dispositivo.
Esse teste fabrica a associação de olho/quadro para exercitar o código;
não é uma chamada real do DLSS nem uma imagem produzida pelo jogo.

A DLL compilou e passaram **107/107 testes em 11,13 segundos**. Os resultados finais
e os comandos estão no registro local `../artifacts/modern-dlss-ownership-port-validation.json`.
Nada foi instalado no jogo. O mod continua **não jogável**, sem teste no Quest
e sem medição de Novigrad. Próximo: acompanhar a reutilização da lista de
comandos e conservar as referências até a submissão e conclusão exatas da
GPU, tratando desligamento/reinicialização do componente antes dos históricos
independentes por olho. A posse temporária não abre o bloqueio do Remastered.

## Trabalho que falta

- Adaptar e verificar os pontos internos de renderização e de jogo para 5.00c.
- Integrar as posições das mãos à aparência das mãos e armas do personagem.
- Integrar trajetória, contato, dano e bloqueio ao combate do jogo.
- Confirmar a ativação de mods e a abertura do jogo com a instalação limpa;
  conferir o complemento recomendado e possíveis conflitos.
- Validar o funcionamento do VR no Quest 3 pelo Virtual Desktop.

O objetivo completo do pedido continua pendente. A DLL atual ainda não permite
jogar em VR; a antiga meta de 60 FPS em Novigrad deixou de ser um requisito.
