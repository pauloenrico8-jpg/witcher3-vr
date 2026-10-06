# Witcher 3 VR para Quest 3 — estado do desenvolvimento

**Ainda não existe uma versão jogável deste projeto com mãos e combate físico.**
Não há medição de FPS em Novigrad. O arquivo compilado está na pasta de
desenvolvimento e não foi instalado no jogo.

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
mãos, espada, contato, bloqueios e transmissão pelo Virtual Desktop. Só então
o save de Novigrad será útil para medir a meta de desempenho em VR.

O critério de desempenho deverá contar imagens novas do jogo para ambos os
olhos: pelo menos 60 por segundo, ou no máximo 16,67 ms por par de imagens.
Também será necessário observar quedas, tempo do processador, placa de vídeo
e transmissão. Imagens repetidas ou geradas artificialmente não demonstram
que o jogo atingiu a meta solicitada. A taxa do headset deve ser anotada
separadamente. Ainda não há resultados para nenhum desses critérios.

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

## Trabalho que falta

- Adaptar e verificar os pontos internos de renderização e de jogo para 5.00c.
- Integrar as posições das mãos à aparência das mãos e armas do personagem.
- Integrar trajetória, contato, dano e bloqueio ao combate do jogo.
- Confirmar a ativação de mods e a abertura do jogo com a instalação limpa;
  conferir o complemento recomendado e possíveis conflitos.
- Validar no Quest 3 e medir Novigrad na máquina identificada.

O objetivo completo do pedido continua pendente. Não há garantia de 60 FPS nem
uma promessa de que basta instalar a DLL atual para jogar.
