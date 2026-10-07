# Witcher 3 VR para Quest 3 — estado do desenvolvimento

**Ainda não existe uma versão jogável deste projeto com mãos e combate físico.**
O usuário cancelou a meta de 60 FPS em Novigrad em 06/10: o foco é concluir o VR,
com estéreo simultâneo, cabeça livre, mãos/armas Touch e combate físico.
Menções à meta nas etapas antigas são registros históricos. O arquivo compilado está na pasta de
desenvolvimento e não foi instalado no jogo.

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
