# Adaptação para Remastered 5.00c — evidências de renderização

Estado em 06/10/2026: análise estática e início da adaptação do código de câmera,
sem liberar o mod no jogo.
“Estática” significa ler o programa como um arquivo, sem executar suas funções.
O objetivo permanece VR com imagens novas nos dois olhos, mãos e armas livres,
combate físico. Em 06/10 o usuário cancelou a meta de 60 FPS em Novigrad;
o foco passou a ser concluir o mod VR. Menções anteriores à meta são históricas.

## Retenção por fila e fence privada — checkpoint de 6 de outubro

`modern_dlss_retirement.h/.cpp` possui gravações compartilhadas imutáveis,
tickets ancorados na identidade própria da fence e Timeline por OwnedQueue.
Prepare exige queue/device canônicos exatos, Stamp e command presente no
array de IDs nativos próprios do envio. BeforeExecute marca possível uso
ANTES do encaminhamento; AfterExecute só sinaliza APÓS seu retorno. O host
deve serializar as observações reais e preservar array/chamada original.
O módulo não chama Execute nem Streamline e ainda NÃO está ligado aos hooks.

Falha de Signal conserva o lote em quarentena e fecha admissão; retry usa um
novo número finito. UINT64_MAX não conclui trabalho. Fence concluída retira
somente esse envio; gravações reenviáveis ficam retidas até Reset observado
com epoch maior. Registro fraco alcança também handles fora de Timeline.
Destrutor transfere estado com envios ou gravações reenviáveis para uma lista
intrusiva sem alocar; coleta exige conclusão própria e abandono comprovado.
Falhas não recuperadas, Execute sem retorno e device removal ficam retidos
até prova posterior/processo terminar. Isso não resolve ainda o shutdown do
SDK, a liberação de heaps/allocators do jogo nem thread ordering dos hooks.

DLL compilada, 107/107 CTest (12,38s), 26 cópias físicas RTX4070Ti com reenvio,
Reset antecipado, fence externa recusada e destruição durante execução.
Falha de Signal injetada é simulação de HRESULT, não falha real do driver.
Recibos/olhos fabricados; jogo/SL/DLSS/Quest não executados. Gate5c/reentrada
fechados. Relatório novo só local; publicação limita-se a código/docs próprios.

## Ferramenta nova e fluxo

`scripts/analyze-remastered-rendering.py` usa a leitura do executável feita por
`scripts/analyze-remastered-callbacks.py` e a mesma dependência Capstone 5.0.6.
Capstone traduz as instruções do processador para uma forma que podemos analisar.

1. Compara o SHA-256 do executável com o relatório de funções já conhecido.
   SHA-256 é um identificador do conteúdo: impede misturar evidências de versões
   diferentes do jogo.
2. Procura os registros de classes do compilador. Esses registros, chamados
   RTTI, identificam os tipos de objetos. Confere vários vínculos e endereços;
   encontrar apenas um nome no arquivo não basta.
3. Lê as tabelas de métodos desses objetos. Uma tabela de métodos contém os
   endereços das funções que o objeto pode chamar. Mantém separados os registros
   que pertencem a partes secundárias do mesmo objeto.
4. Usa os registros de recuperação de pilha do Windows para reunir os blocos de
   cada função. A pilha guarda dados temporários das chamadas; seus registros
   mostram que um bloco intermediário pertence a uma função iniciada antes.
   Cadeias quebradas ou cíclicas são recusadas.
5. Segue os desvios diretos do código e para em retornos. Isso evita interpretar
   tabelas de dados embutidas como instruções. Se um desvio depende de um valor
   calculado, registra que seu destino ainda não foi resolvido.
6. Grava nomes de classes, endereços, blocos, chamadas e acessos a campos para
   revisão. Os acessos a campos são observações de instruções: seus significados
   e o comportamento em execução ainda precisam ser conferidos.

Foi escolhida essa abordagem porque o 5.00c contém funções divididas em vários
blocos e tabelas dentro das regiões de código. Procurar bytes e aplicar um
deslocamento geral aos endereços da versão 4.04 perderia essas diferenças.

O formato de encadeamento segue a
[documentação do Windows x64](https://learn.microsoft.com/en-us/cpp/build/exception-handling-x64).
A ferramenta identifica os três registros simples de versão 2 presentes neste
executável; não interpreta cadeias de versão 2 nem formatos desconhecidos.
Não há código dessa ferramenta na DLL que roda dentro do jogo.

## Descobertas verificadas no arquivo

O relatório local é `../artifacts/remastered-rendering-evidence.json`.
Foi gerado para o executável SHA-256
`9406ECCC12B68E08920931442EF6A57340E910D3E01F2082E88232487433FE51`.
Contém 15 tabelas de classes, 43 referências de código a essas tabelas e
145 entradas no grafo limitado de funções. Essas quantidades não representam
uma porcentagem de adaptação do mod.

| Ponto | Evidência no 5.00c | Consequência |
| --- | --- | --- |
| `GetCameraDirection` em `0x0237A1C0` | Seu registrador é um método de `CCameraDirectorClassBuilder` | A classe foi distinguida da outra função de mesmo nome. |
| `GetCameraDirection` em `0x02470440` | Seu registrador pertence a `CCameraClassBuilder` | Não substituir automaticamente o callback do diretor por esse endereço. |
| Tabela principal de `CCustomCamera` | `0x03805378`, com deslocamento de subobjeto zero | Candidato para a identidade de câmera; existem outras tabelas do mesmo objeto nos deslocamentos `0x10` e `0x170`. |
| Comando `CRenderCommand_RenderScene` | Tabela `0x0394FFE8`; método em `0x01D054E0` chama `0x01C13630` em `0x01D0554C` | Há uma ligação real entre o comando de cena e a rotina de renderização, com três argumentos preparados pelo chamador; contrato completo ainda em revisão. |
| Construção de `CRenderFrame` | `0x01B80550`, slot 65 (`+0x208`) de `CRenderInterface`, instala a tabela `0x03958958` e aloca `0xF910` bytes | A chamada de criação foi localizada no produtor de cena em `0x022758AF`; campos e restante da cadeia ainda precisam da adaptação. |
| Cópia/reconstrução de câmera em `0x0228A970` | Chama `0x0228AB40` e preserva a fórmula de escala de distância com coeficiente `3.000002861022949` | Candidato forte para comparar com `kEngineViewCopyRebuildRva`; parâmetros e todos os campos ainda exigem revisão. |
| Registro temporal da câmera | A cópia em `0x0228A970` desloca origem e destino até `+0x530` antes de copiar o registro | O código 4.04 usa `+0x460`; mudar apenas o endereço da função conservaria um acesso incompatível. |
| Cópia do descritor em `0x00324430` | Há leitura de um campo em `+0xF748` | O prefixo antigo de `0xC000` bytes não cobre todos os campos observados desse objeto. |
| Construtor temporal em `0x02288F00` | Chamador `0x022B6337` prepara tempo, posição, orientação e parâmetros de projeção; resultado ocupa campos até `+0xAC` | Candidato para comparar com o construtor temporal de nove argumentos do código antigo. |

### Ligação da fábrica ao produtor de cena

A função `0x02274B80` está no slot 48 (`+0x180`) da tabela principal de
`CGame`, `0x03948EE0`. Sua chamada em `0x022758AF` carrega o renderizador pelo
ponteiro global em `0x05A51950` e usa o slot 65 da tabela desse objeto. Na tabela
principal de `CRenderInterface`, `0x036E03C8`, esse slot aponta para `0x01B80550`.
Isso liga estaticamente o produtor de cena à criação de `CRenderFrame`.

A inicialização reforça a identidade do objeto: em `0x0227D864` há chamada ao
construtor `0x01BDC9D0`, seguida de armazenamento do resultado no global
`0x05A51950` em `0x0227D869`. Esse construtor instala a tabela de
`CRenderInterface` com referência em `0x01BDC9E6`. O armazenamento do motor em
`0x05A518F8`, `0x01B73079`, recebe o resultado de `0x01D62F50`, que instala a
tabela de `CR4GameEngine` com referência em `0x01D62F74`. São ligações de
inicialização no arquivo, ainda sem inspeção dos objetos em execução.

O chamador passa o descritor no terceiro argumento e zera o segundo. A fábrica
também substitui o segundo registrador pela leitura de um ponteiro no início do
descritor. Portanto, a exigência antiga de `render_settings != nullptr` não pode
ser levada diretamente para esse caminho. A chamada anterior em `0x022755A1`,
com slot `+0x290`, pertence a outra operação; não é a chamada da fábrica.

Depois da criação, `0x022758EB` chama slot 13 (`+0x68`) do objeto global
`0x05A518F8`. As tabelas de `CBaseEngine` e `CR4GameEngine` ligam esse slot a
`0x0224C600`, que prepara dados auxiliares do frame. **Essa chamada não foi
identificada como envio do frame para renderização.** O produtor cria o comando
de cena em seguida: aloca com `0x00395D80`, chama seu construtor `0x02297670` em
`0x0227593C` e despacha pelo slot 1 (`+0x08`) em `0x0227594C`. O construtor instala
`CRenderCommand_RenderScene`, guarda frame/auxiliar em `+0x08`/`+0x10` e incrementa
suas referências. O produtor libera sua referência ao frame pelo slot 2.

Há outra rotina em `0x022B6420` que monta esse comando, mas usa um campo do
objeto em `+0x5440`. O objeto que ela recebe não foi provado como o renderizador
recebido pela fábrica. Não chamar essa rotina com o primeiro argumento da
fábrica apenas por semelhança com a versão antiga. A vida útil do comando,
condições do produtor e rotas auxiliares ainda precisam ser conferidas.

Esses registros estão locais em `../artifacts/remastered-frame-production-route.json`.
A busca de referências agora também reconhece instruções que gravam ponteiros
globais, além das que os leem. Confere o começo real das instruções para não
confundir dados internos ou bytes após um retorno com essas gravações.

Na cópia do descritor, a última leitura direta observada é **um byte** em
`+0xF748`. Isso exige pelo menos `0xF749` bytes para esse acesso; não determina
sozinho o tamanho completo do objeto nem seus objetos apontados. A cópia antiga
de `0xC000`/`0xBD00` continua insuficiente.

Os endereços da tabela são relativos ao início do executável, chamados RVA.
Eles são específicos do SHA-256 registrado. Nenhum foi usado para ativar um hook,
isto é, uma ligação que intercepta uma função do jogo enquanto ela executa.

Os 15 registros de nomes/callbacks do relatório anterior foram reanalisados com
o novo tratamento de blocos e fluxo. Seus candidatos permaneceram iguais.
Registro local: `../artifacts/remastered-callback-controlflow-evidence.json`.

## Primeiro acesso de câmera adaptado por versão

`src/engine_camera_temporal.h` reúne a construção e a escrita do registro da
câmera anterior. Esse registro ocupa `0xB0` bytes. O contrato 4.04 guarda-o em
`+0x460`; o contrato examinado para 5.00c guarda-o em `+0x530`. Isso não define
o layout completo da câmera, do frame ou do descritor da cena.

No chamador `0x022B6337`, o destino é passado em RCX, o tempo em XMM1, posição
em R8 e orientação em R9. Os cinco argumentos na pilha são, em ordem:
FOV (`+0x1C` da câmera), aspecto (`+0x28`), plano próximo (`+0x30`), plano
distante (`+0x34`) e escala de projeção (`+0x2C`). O construtor `0x02288F00`
devolve o destino, marca o primeiro byte e escreve até o campo em `+0xAC`.
Essa ordem corresponde ao tipo de chamada usado pelo projeto antigo.

Os dois caminhos de histórico por olho em `dxgi_proxy.cpp` agora usam esse
módulo. Leem apenas o prefixo de entrada de `0x38` bytes, preservam quatro
componentes nos vetores, recusam entradas não finitas/projeções degeneradas e
constroem o resultado num espaço temporário alinhado a 16 bytes. Publicam o
histórico somente se o construtor devolve esse espaço com a marca válida e se
a escrita na câmera tem sucesso. Os endereços dos três hooks de construção,
reconstrução e cópia/reconstrução também vêm do contrato de câmera.

**A inicialização continua escolhendo apenas o contrato 4.04 depois da
verificação antiga. O contrato 5.00c não é escolhido automaticamente nem
libera os demais hooks.** Há outros campos e endereços antigos no caminho.
O módulo novo não confirma compatibilidade completa nem captura memória de
objetos desconhecidos; seus limites de bytes dependem do objeto correto fornecido
pelo hook nativo. A validação por intervalo não substitui essa identidade.

O teste `engine_camera_temporal_contract` usa uma função nativa simulada para
conferir a ordem dos argumentos, alinhamento, entrada truncada, valores inválidos,
resultados incompletos e limites da escrita. Confere todos os bytes fora do
registro das duas versões; o caso 5.00c preserva a área antiga `+0x460`.
Não executa as funções do jogo.

Evidência estática desta revisão, com identificadores dos blocos e chamadas:
`../artifacts/remastered-temporal-contract-evidence.json`, somente local.
A DLL compilou; a suíte completa passou em 98/98 na revisão final.
Log: `../artifacts/test-camera-contract-final-20261006.log`.

## Rota de envio de comandos de cena

`src/engine_frame_submission.h` prepara a rota nativa examinada em 5.00c.
O envio do frame duplicado em `dxgi_proxy.cpp` agora escolhe a rota pelo
contrato já publicado na inicialização. **Apenas 4.04 continua sendo escolhido;
a rota Remastered permanece sem ativação.** A rota antiga conserva o envio
com seu receptor original e a liberação da referência criada pela fábrica.

A rota nova executa estas etapas:

1. Confere se o renderizador conhecido está presente e se as chamadas exigidas
   estão disponíveis. Recusa a rota incompleta antes de alocar um comando.
2. Usa `0x00395D80`, o alocador de comandos do jogo. Ele fornece um comando de
   `0x18` bytes com dados da fila antes do objeto. Um objeto criado com `new`
   não teria esses dados e não serviria para o despacho nativo.
3. Recusa resultado nulo e o objeto estático `0x05C88BF0`. Duas referências
   no alocador e uma no despachante apontam para esse mesmo objeto; o
   despachante o exclui. Não construir nele um comando que retenha o frame.
4. Chama `0x02297670` para construir o comando na alocação nativa, com o frame
   em seu campo `+8` e objeto auxiliar nulo, como no produtor `CGame` examinado.
   O construtor incrementa a referência ao frame antes do envio.
5. Chama `0x022925E0` para despachar essa mesma alocação. Essa função usa dados
   da fila antes do comando e não devolve uma confirmação de imagem renderizada.
6. Libera a referência própria recebida da fábrica. A referência do comando
   permanece separada; o destrutor nativo `0x00890B60` a libera pelo slot 2.

Se faltar uma dependência ou a alocação falhar, o código libera a referência
própria ao frame sem construir ou despachar o comando. Retira as identidades
do par incompleto e aplica o intervalo de tentativa já usado pelo produtor.
O resultado "despachado" descreve o retorno da chamada; não comprova execução
da fila, conclusão na placa de vídeo nem imagens corretas no Quest.

Os testes simulam a referência mantida pelo comando, conclusão imediata ou
posterior, dependências ausentes e o objeto estático inválido. Não executam
o alocador nem o despachante reais. A identificação do slot de liberação foi
obtida da tabela do frame e dos chamadores; o método `0x0228E930` não tem registro
de recuperação de pilha, e a ferramenta não infere seu corpo por proximidade.

Evidência local: `../artifacts/remastered-frame-command-contract-evidence.json`.
Ainda é necessário adaptar o descritor completo, as etapas de preparação do
produtor e os demais caminhos de câmera/renderização antes de escolher 5.00c.
Essa rota isolada não substitui essas etapas nem comprova VR em execução.
A DLL compilou e a suíte completa passou em 99/99, com log em
`../artifacts/test-frame-submission-20261006.log`.

## Argumentos e admissão da fábrica

`src/engine_scene_factory.h` separa o endereço da fábrica e suas condições de
entrada por versão. A rotina 5.00c usa `0x01B80550`; lê largura e altura no
descritor, em `+0xBDC` e `+0xBE0`, e devolve nulo se alguma for zero.
Carrega configurações do campo `+0` do descritor e admite esse campo nulo.
O segundo argumento recebido do produtor é nulo em `0x0227589D/0x022758AF`.

Quatro decisões de início/identificação de cena na DLL agora usam a condição
da versão escolhida. 4.04 conserva a exigência antiga do segundo argumento.
5.00c exige renderizador e descritor presentes, dimensões legíveis e não nulas;
não exige o segundo argumento preenchido. A leitura nova se limita aos oito
bytes de largura/altura. Uma versão desconhecida não instala a fábrica.
Apenas 4.04 continua selecionado após o preflight; o bloqueio de 5.00c permanece.

Outra construção de frame, `0x022C65B0`, chama a cópia nativa do descritor em
`0x022C65E7` e grava dimensões em frame `+0xF760` em `0x022C65F7`. Junto com a
fábrica, isso sustenta a região inferida de `0xF750` bytes a partir de `+0x10`.
**Não comprova o tamanho completo, acessos indiretos ou a vida útil dos objetos
apontados.** A procura por um tamanho imediato `0xF750` não encontrou uma
instrução alcançada que confirmasse esse tamanho. A cópia antiga permanece
inadequada para a versão nova e ainda não foi liberada ou substituída por
um tamanho presumido.

Evidência local: `../artifacts/remastered-factory-admission-evidence.json`.
O teste de admissão usa dimensões/argumentos simulados, sem execução do jogo.
A DLL compilou; suíte completa 100/100 em
`../artifacts/test-factory-admission-20261006.log`.

## Descritor completo observado e cópia emprestada

`src/engine_scene_descriptor.h` define a política usada pelo hook da fábrica.
Mantém `C000`/mínimo `B000` e o prefixo TAAU `BD00` para 4.04. Em 5.00c, usa
a região `F750` encontrada entre descriptor em frame `+10` e dimensões em
`+F760`, exige leitura completa e alinhamento de 16 bytes. Não aceita aplicar
o prefixo antigo à versão nova. O tamanho é um contrato da região observada;
não é uma definição completa dos tipos internos nem uma liberação de versão.

Revisão das partes chamadas por `00324430`:

| Região de origem | Rotina de cópia | Limite observado |
| --- | --- | --- |
| `+10` e `+5F0` | `0228A970` | Câmeras, incluindo histórico temporal; próximas regiões em `5F0`/`BD0`. |
| `+C70` | `00324C90` | Leitura direta termina antes de `F60`. Há retenção de referências a recursos. |
| `+F60`, `+58F0`, `+A280` | `00325460` | Blocos separados por `4990`; filho final em `+41E0` lê até `+7A4`, dentro do bloco. |
| `+EC20` | `0032BCA0` | Lista com contagem em `ECA0`, dados em `ECB0`, elementos de `60` bytes. O campo seguinte em `EE30` limita a quatro elementos. O código recusa contagens maiores. |
| `+F020` | `0032BDC0` | Próxima região em `F3B0`; filhos `0032C160` copiam listas apontadas por meio de `0032F260`. |
| `+F6F0` | `0032C200` | Leitura direta termina em `F734`; retém o recurso apontado em `+30`. |
| `+F748` | `00324430` | Último byte fixo observado; armazenamento termina em `F750` nos dois construtores. |

Os laços fixos restantes do copiador e os filhos dos blocos foram revisados.
Listas apontadas ficam fora do bloco: seu conteúdo pertence ao produtor
original e a fábrica nativa faz suas próprias cópias/referências. Por exemplo,
`026DD530` retém um recurso, aloca dados e copia uma lista apontada; `0032C160`
inicializa outro contêiner e usa `0032F260` para copiar os elementos.
O primeiro construtor faz duas cópias nativas síncronas em `01B80676` e
`01B80704`. Isso sustenta a política de entrada emprestada, sem chamar
destrutores nativos sobre uma cópia simples de bytes.

O descritor 5.00c fica em um vetor local à chamada da fábrica, sem compartilhar
o anel antigo de cópias. Depois das chamadas e dos diagnósticos síncronos, o
vetor libera somente seu armazenamento. Não amplia a duração dos objetos
apontados. **Essa duração ainda não foi validada dentro do jogo.**

Evidência local: `../artifacts/remastered-descriptor-leaf-evidence.json`.
O relatório conserva `DescriptorLayoutFullyVerified=false`: nem todos os
significados dos campos e tipos apontados foram definidos. O contrato cobre
a cópia observada, sob as condições descritas; não cobre o restante da renderização.

## Rotinas pequenas fora do índice de funções

Windows x64 permite rotinas que não alteram a pilha nem registradores
preservados sem registros de recuperação. Isso explica por que a análise
anterior deixava alguns destinos sem corpo, conforme a
[documentação da convenção x64](https://learn.microsoft.com/en-us/cpp/build/x64-calling-convention).

`direct_call_leaf_facts` exige uma instrução CALL direta alcançada em um
chamador com metadados conferidos. Ela usa o destino dessa instrução, não bytes
vizinhos, como entrada. Confere as duas rotas de desvios condicionais dentro
de limites de bytes/instruções; para em retornos e em saltos não resolvidos;
recusa alterações de pilha, registradores preservados, chamadas internas,
instruções sobrepostas e saídas da região permitida. O resultado não inventa
um registro de função: `PrimaryEntryRva` continua nulo.

Foram examinadas 28 chamadas para essas rotinas, sem ativar seus endereços.
Para reproduzir uma delas com a ferramenta existente, acrescente
`--inspect-leaf-call 0x00324683` ao comando de análise abaixo. Esse parâmetro
é o endereço da chamada, e não o destino `00324C90`.

## Limites que continuam abertos

- As tabelas identificam classes, mas não provam todos os campos, argumentos,
  chamadores e condições usados pelo mod.
- Os grafos têm profundidade limitada. Destinos de tabelas de desvios e chamadas
  virtuais precisam de análise adicional; não são inferidos automaticamente.
- O callback de script `GetCurrentViewportResolution` continua distinto do hook
  nativo de resolução que recebe quatro argumentos no projeto original.
- Ainda faltam as rotas de constantes de renderização, históricos temporais,
  layouts da fábrica, passagem dos dois olhos, carregamento e proprietários dos
  objetos. A DLL continua recusando a versão incompatível.
- Não há execução no Quest, integração de combate físico ou medição de Novigrad.

## Validação e reprodução

`python tests/remastered_analysis_tests.py -v` passou em 37 testes independentes.
Eles verificam blocos separados, alinhamento dos registros, cadeias inválidas,
arquivos truncados, tabelas de classes, classes com nomes de callback iguais,
falsas chamadas dentro de dados ou depois de um retorno, e a recusa de caminhos
de saída que sobrescreveriam o executável ou o relatório de entrada.
Três casos adicionais conferem gravações globais reais e falsas gravações
dentro de dados ou após retorno. O registro atual é
`../artifacts/test-leaf-analysis-20261006.log`. Os 12 casos adicionais verificam
a entrada por chamada real, caminhos condicionais, preservação da pilha,
limites, arquivos truncados e a recusa de chamadas dentro de dados ou após retorno.
Esses testes não substituem os 101 testes C++ registrados em
`../artifacts/test-descriptor-final-20261006.log` nem
demonstram que o jogo abre como VR.

Com a dependência de análise disponível em `.analysis-packages`, executar na
pasta do repositório, substituindo `<jogo>` pela pasta real da instalação:

```powershell
python scripts/analyze-remastered-rendering.py `
  --exe "<jogo>/bin/x64_dx12/witcher3.exe" `
  --callbacks diagnostics/remastered-callback-evidence.json `
  --report ../artifacts/remastered-rendering-evidence.json `
  --inspect-rva 0x0228A970 --inspect-rva 0x0228AB40 `
  --inspect-rva 0x01B80550 --inspect-rva 0x01C13630 `
  --inspect-rva 0x022B6420 --inspect-rva 0x02274B80 `
  --inspect-rva 0x02288F00 --call-depth 1
```

## Preparação adiada no produtor 5.00c

O produtor base é `02274B80`, chamado por `01F43750` em `01F437D6` com o
objeto CGame em RCX e o intervalo de tempo em XMM1. A tabela primária de CGame
`03948EE0`, slot 48, também aponta para esse produtor; CR4Game passa pelo
wrapper `01D6ED30`. O hook usa o argumento real da chamada. O global
`05A51930` usado pelos efeitos não foi estabelecido como CGame.

Após a fábrica retornar em `022758B5`, a preparação natural é:

| Etapa | Chamada | Retorno usado pelo observador |
|---|---|---|
| Callback opcional: CGame+100, receiver em +8, slot 10 | `022758DB` | `022758DE` |
| Engine global `05A518F8`, slot 13, frame em RDX | `022758EB` -> `0224C600` | `022758EE` |
| Avanço compartilhado dos efeitos | `0227591C` -> `02367890` | `02275921` |
| Aplicação de efeitos à imagem, frame em RDX | `02275924` -> `02367B60` | `02275929` |
| Construtor de comando, auxiliar nulo | `0227593C` -> `02297670` | `02275941` |

`02367890` soma o tempo em campos do estado global de efeitos `05A51E98`,
incluindo +64/+68/+6C. Reexecutar o produtor completo ou esse avanço para o
outro olho alteraria a simulação. O novo caminho observa essa chamada, mas
reexecuta somente a preparação de recursos e a aplicação à imagem.

O callback opcional é aceito somente se o receiver estiver ausente ou seu
slot 10 apontar para `0031D810`, contendo `C2 00 00` (retorno sem alterar a
pilha). O destino foi examinado pelas tabelas primárias conferidas de CWorld
e CGameWorld, não por uma CALL direta inventada nem por bytes vizinhos. Seu
registro de unwind continua ausente. Essa condição permite omitir somente
esse callback comprovadamente vazio, sem supor o tipo de um receiver novo.

`0224C600` usa Engine+40 em um callback sem argumento de frame e depois
prepara recursos da imagem. O caminho novo exige Engine+40 nulo em três
pontos: antes das fábricas, após a preparação natural e antes da repetição.
Engine tem a tabela examinada `037A8F18` e slot 13 correspondente; o contexto
do mundo e os globais de efeitos também precisam manter a identidade.
Callbacks adicionais ainda precisam de classificação antes de suportá-los.

`RemasteredProducerScope` guarda uma imagem adicional por execução, usando
estado local à thread e restaurando o produtor pai ao sair. As duas fábricas
continuam síncronas; só o frame nativo com referência própria fica pendente,
nunca o descritor emprestado. `PendingPair` verifica a ordem observada e
consome essa referência no despacho ou cancelamento. O segundo olho só é
preparado depois de o construtor natural ter retido a imagem principal.
Repetições usam trampolines (acesso à função original preservado pelo hook),
evitando passar pelos observadores outra vez. A instalação dos cinco hooks
é agrupada e só autoriza a fábrica após todos estarem prontos.

O envio imediato antigo não autoriza o caminho 5.00c. Chamadas auxiliares de
fábrica fora do retorno nativo conferido não geram um segundo olho. Falta de
etapas, chamadas repetidas, mudança de contexto ou saída antecipada cancelam
a imagem e suas tags. Falha do alocador nativo também libera a referência.
Os diagnósticos antigos de scheduler/renderer não leem seus offsets 4.04
nesse ramo novo.

**Isso continua sem execução no Remastered.** `02367B60` também escreve caches
globais e chama funções nativas/indiretas; sua repetição e a vida dos recursos
não foram validadas no jogo. Os testes simulados verificam somente o contrato
de ordem e posse. Não foi liberado o bloqueio que ainda seleciona apenas
4.04. Evidência local: `../artifacts/remastered-preparation-evidence.json`.

A DLL passou na compilação e a suíte final passou em 102/102 testes do CTest.
Logs: `../artifacts/build-preparation-final-20261006.log` e
`../artifacts/test-preparation-final-20261006.log`. Os 37 testes de análise
registrados anteriormente permanecem uma verificação distinta.

O próximo passo é continuar os campos da câmera secundária (novo desc+5F0,
frame+600), constantes de renderização, jitter e a rota da tarefa nativa.
Os callbacks adicionais e caches dos efeitos também precisam de verificação.
Não liberar o bloqueio por contratos isolados; duração dos recursos e imagens
reais precisam de verificação em execução.

## Mapa de campos e classificação do escritor de câmera

`src/engine_camera_layout.h` separa posições por versão, usando o contrato
temporal já selecionado. Não seleciona a versão instalada nem autoriza hooks.
Inicialização continua publicando somente o contrato 4.04 após o preflight.

| Campo/origem | 4.04 | Remastered 5.00c |
|---|---|---|
| Tamanho da câmera | 510 | 5E0 |
| Câmeras no descritor | 10 / 520 | 10 / 5F0 |
| Descritor dentro do frame | 10 | 10 |
| Câmeras dentro do frame | 20 / 530 | 20 / 600 |
| Jitter dentro da câmera | 400 / 404 | 4C0 / 4C4 |
| Viewport dentro da câmera | 408 / 40C | 4C8 / 4CC |
| Registro anterior / FOV anterior | 460 / 468 | 530 / 538 |
| Extent de entrada no descritor / frame | A3C / A4C | BDC / BEC |

Os valores são offsets hexadecimais, não endereços absolutos.
Os dois registros internos pertencem ao mesmo descritor/frame; não representam
por si só os dois olhos do HMD, que precisam de frames renderizados separados.

`00324430` chama a cópia `0228A970` para desc+10 e desc+5F0. A cópia transfere os campos
4C0/4C4/4C8/4CC e reconstrói a câmera com `0228AB40`. O rebuild lê esses
campos em `0228B6D3` a `0228B71F`; divide duas vezes o deslocamento pela
largura/altura, com limite mínimo 1. Sinal e convenções ao longo da rota ainda
precisam de prova. O bloco 400 é escrito como matriz em `0228B5DB..0228B6CB`.
Os blocos de matriz dos snapshots continuam apenas registros de bytes;
não foram estabelecidos como um contrato novo de escrita ou culling.

`022B51E0` tem o ABI observado de cinco argumentos: RCX descritor, XMM1 e
XMM2 valores, R9D largura e quinto argumento altura em entrada RSP+28.
Grava os mesmos valores nas duas câmeras e reconstrói ambas. A análise inicial
seguia somente três chamadas. A atualização abaixo resolve uma tabela de modos
e encontra também a chamada da cena normal. As quatro chamadas verificadas
têm propósitos distintos:

| Retorno após CALL | Classe primária verificada / propósito |
|---|---|
| 01C14BD1 | Core normal 01C13630: jitter atual calculado para a cena |
| 01D59287 | RenderUberSampleNormalTaskBatch: aplicar amostra |
| 01D59880 | A mesma tarefa: restaurar valores capturados antes do loop |
| 01D59D27 | RenderFinal2DTaskBatch: substituição opcional de valores |

As tabelas primárias são `037A6C20` (slot 2 -> `01D590A0`) e `037A6C00`
(slot 2 -> `01D59CD0`), verificadas por COL/RTTI, isto é, registros de tipos
do executável. Essas chamadas não substituem uma a uma as oito rotas antigas.
A instalação usa a entrada correspondente ao contrato aceito pela inicialização;
o preflight continua recusando Remastered. Os registros de autoridade/culling
antigos não foram autorizados para a versão nova.

Os snapshots e três verificadores de campos das câmeras agora usam o mapa;
o registro da fábrica guarda o mapa junto com a amostra. Leituras truncadas
ou campos fora do intervalo não publicam resultados parciais. Um contrato
nulo ou desconhecido não recebe offsets antigos como fallback.

Compilação e 103/103 CTest passaram. O teste novo inclui dados deliberadamente
diferentes em +400/+468 na câmera nova, câmeras primária/secundária distintas,
origens frame/descritor, desalinhamento, truncamento, overflow e recusa de
contratos não selecionados. É uma verificação de memória simulada no PC,
sem execução do jogo. Logs locais `../artifacts/*camera-layout-final-20261006.log`;
evidência local `../artifacts/remastered-camera-layout-evidence.json`.

Outra candidata, `01C1D550`, escreve jitter diretamente em frame+4E0/AC0 e
usa extent fixo 3840x2160. O chamador `01D03120` é slot 2 da tabela primária
verificada `0394FA88`, CRenderCommand_TakeUberScreenshot. Essa rota de
captura também não deve ser promovida à rota temporal da imagem normal.
Sua evidência continua local em `../artifacts/remastered-inline-jitter-candidate.json`.

Essa etapa inicial foi seguida pela investigação abaixo.
O comando normal chama o core `01C13630` em `01D0554C`: o retorno correto
é `01D05551`, corrigindo a anotação anterior do handoff. Os demais hooks de
câmera, constantes, caches dos efeitos e recursos continuam pendentes.

## Rota temporal normal e tabelas de modos: 6 de outubro

O analisador agora segue somente uma forma verificada de tabela de desvios:
comparação sem sinal limitada a até 256 entradas, ramo de fallback, base da
imagem, leitura de RVAs de 32 bits, soma com a base e salto. Todos os destinos
devem ficar nos trechos da mesma função, fora dos bytes da tabela. Tabelas
truncadas, destinos de outras funções e instruções sobrepostas são recusados.
Outras formas de salto continuam marcadas como não resolvidas.

No core `01C13630`, o salto `01C14B72` usa nove entradas em `01C15854`.
Os modos 0/1 seguem `01C14C0C`; 2..8 seguem `01C14B74`. Não foi inferido um
nome de backend para todos esses números. Nessa segunda rota:

1. `01C03F10` calcula jitter para o índice renderer+7F0, e outra chamada
   calcula o índice anterior. O descritor é frame+10, provado em `01C13B1B`.
2. `01C14BCC` chama `022B51E0` com jitter atual e dimensões desc+BDC/BE0.
   O retorno é **01C14BD1**, não o endereço da CALL.
3. Depois do retorno, o core grava jitter anterior em desc+4E0/4E4 e suas
   dimensões em desc+4E8/4EC. Não confundir esses campos com o jitter atual
   desc+4D0/AB0, nem com o registro histórico desc+540.
4. `01C14C21` chama `01B799C0` com estado renderer+C0, descritor e flag R8B.
   Essa função usa frame-id desc+C44 e guarda o identificador em estado+120.
   Copia uma câmera privada antes de montar as constantes; a reentrada e os
   demais campos do estado ainda precisam ser adaptados e verificados.

O hook do escritor agora escolhe entrada, lista de retornos e campos de jitter
por contrato. O retorno normal novo pode participar da rota; as três chamadas
de supersampling/restauração/Final2D não recebem outro deslocamento óptico.
A dica antiga de valor já centralizado continua exclusiva de 4.04. A conferência
do escritor lê jitter atual em desc+410/920 para 4.04 e desc+4D0/AB0 para 5.00c.
As capturas antigas de auditoria ficam restritas a 4.04. Isso prepara o hook,
mas **não libera a inicialização em 5.00c nem comprova a projeção no jogo**.

RenderNormalEpilogueTaskBatch chama `01C21200`, mas suas reconstruções
`01C236FD/01C23733` usam uma cópia privada na pilha (RBP+9B0), obtida em
`01C236C3`. Elas não escrevem as duas câmeras vivas da cena e não foram
promovidas a uma rota normal de escrita.

Evidência local: `../artifacts/remastered-normal-camera-switch-evidence.json`.
Ela não foi publicada. Os relatórios antigos mantêm sua condição de evidência
parcial; o número inicial de três chamadas não era uma prova de completude.
Validação: DLL compilada, 103/103 CTest e 43/43 testes Python passaram.
Os seis testes Python novos exercitam chamadas escondidas atrás da tabela,
limites, base errada, outro dono e sobreposições. O teste de câmera confere
as quatro classificações, a recusa de retornos de restauração/2D e a separação
entre campos atuais, anteriores e da versão antiga. Todos usam dados do PC;
nenhum mede execução no jogo ou no headset.

Próximo passo: adaptar a preparação das constantes `01B799C0`, distinguir
estado/renderizador/descritor, verificar a escrita do jitter anterior e portar
as demais leituras e matrizes do frame builder. O bloqueio global permanece.

## Builder e ABI de constantes Streamline: 6 de outubro

`engine_view_constants_contract.h` seleciona somente os contratos canônicos:
4.04 builder `01CFE280`, frame-id desc+AA4, guard estado+6C; 5.00c builder
`01B799C0`, frame-id desc+C44, guard estado+120. O descritor não é o frame;
o estado não é o renderizador. Nenhum getter global legado, guard de entrada
ou guard de avaliação foi atribuído ao novo estado por deslocamento presumido.

O hook novo observa RCX/RDX apenas sob RETURN `01C14C26`, com olho/par/geração
válidos. O chamador obtém RCX de renderer+C0 e passa RDX=frame+10. A observação
fica restrita à chamada original e restaura a anterior ao sair, inclusive em
chamadas aninhadas. A instalação escolhe a entrada por contrato e exige o gate
prévio. Probes de matrizes antigos e a reentrada antiga ficam exclusivos de 4.04.

As importações verificadas no executável são `slGetNewFrameToken` em IAT
`02984CB0` e `slSetConstants` em `02984CB8`, ambas de `sl.interposer.dll`.
A CALL `01B79C45` recebe endereço do token e endereço do frame-id. A CALL
`01B7A5E4` recebe constantes RBP+240, token opaco [RBP+210] e objeto viewport
RBP+218; RETURN correto `01B7A5EA`. Nenhum ponteiro é reduzido a uint32_t.

Construtor leaf `00320940`, chamado em `01B79CD4`, foi decodificado só até seu
primeiro RET `00320D50`. Ele inicializa o GUID Constants, versão 2 e campo final
em +1C4. O layout confirma header de 0x20 bytes (32 bytes), jitter +160, escala de vetores
+168, reset +1BF e tamanho mínimo 1C8. Tipo/versão/sinalizador são conferidos
antes da leitura; falhas preservam a saída. O callback novo encaminha os três
ponteiros originais sem alterar constantes ou viewport, devolve o resultado
real e publica recibo somente após sucesso e identidade temporal exata. Seu
número de recibo vem do descritor observado, não de uma suposta leitura do token.

A documentação oficial sustenta a interpretação do ABI e do header:
[API](https://github.com/NVIDIA-RTX/Streamline/blob/main/include/sl_core_api.h),
[estruturas](https://github.com/NVIDIA-RTX/Streamline/blob/main/include/sl_struct.h),
[constantes](https://github.com/NVIDIA-RTX/Streamline/blob/main/include/sl_consts.h).
A escolha do layout do jogo vem da evidência local, não do cabeçalho atual sozinho.

O instalador moderno só pode criar o observador de constantes; não cai na
instalação de slSetTag/slSetFeatureConstants/slEvaluateFeature antigos. Falha
na ativação remove a interceptação recém-criada. A inicialização global ainda
rejeita 5.00c antes de instalar qualquer hook. Reentrada moderna fica fechada
até tags, viewport e avaliação serem portados juntos; observar um sucesso não
comprova isolamento de históricos nem conclusão na GPU. O callback legado agora
também devolve o resultado real e publica recibo apenas após sucesso.

Validação: build da DLL e teste novo, 104/104 CTest (14,31s), diff sem erros.
Logs locais `../artifacts/*view-constants-20261006.log`. Relatório novo LOCAL
`../artifacts/remastered-view-constants-abi-evidence.json`, não publicado.
Próximo passo: verificar o produtor de tags e avaliação modernos e o pipeline
candidato `01D2B640`, mapear separadamente os guards e conservar os objetos
FrameToken/ViewportHandle em todas as chamadas. Nenhum teste de jogo/Quest/FPS.

## ABI de tags/avaliação e contadores distintos: 6 de outubro

A varredura das CALLs RIP FF15 da IAT, aceitas somente em instruções
decodificadas de funções com unwind, identificou `slSetTagForFrame` (IAT
`02984CF0`) e `slEvaluateFeature` (`02984CF8`). O produtor de tags é `01ED28F0`,
com RETURNs `01ED2ADA` e `01ED2BA0`, incluindo seu caminho de remoção.
O avaliador `01ED2C00` chama feature 0 em `01ED322A` (RETURN `01ED3230`).
O avaliador `01ED32B0` chama feature 1001 em `01ED3F8A` (RETURN `01ED3F90`).
São DLSS e DLSS Ray Reconstruction respectivamente, não geração de quadros.
Essa evidência não prova que todos os caminhos dinâmicos foram encontrados.

As funções nativas `01B77EB0` e `01B77890` verificam estado+124 contra
descritor+C40. A preparação `01B78CE0` usa estado+128 contra C40 e pode
atualizar +124 quando a entrada já foi preparada. O builder usa +120/C44.
`engine_dlss_contract.h` mantém C40 e C44 independentes, lê ambos os guards
sem escrevê-los e aceita somente o contrato canônico 5.00c. Não existe um
deslocamento único em relação ao layout antigo. O cooldown em estado+17C
e o receptor dos métodos ainda precisam ser compreendidos; não são zerados.

O ABI moderno de tags é `(token*, viewport*, tags*, count, command*)`; o de
avaliação é `(feature, token*, inputs**, count, command*)`. Os novos tipos e
encaminhadores não compartilham trampolines legados, preservam ponteiros de
64 bits, arrays, quinta posição do comando, contagens e retorno real. Não
interpretam ResourceTag/Resource usando os offsets antigos. Nenhum token é
desreferenciado. A leitura de viewport exige header sem cadeia, GUID
`171B6435-9B3C-4FC8-9994-FBE52569AAA4`, versão 1 e id diferente de UINT_MAX.
Lê +20 em um objeto de 0x28 bytes. Dados desconhecidos são encaminhados intactos.
A avaliação interpreta apenas o caminho nativo examinado de uma entrada;
arrays arbitrários continuam sendo repassados inteiros.

A instalação moderna cria constantes, tags e avaliação, publica readiness
somente quando todos foram ativados e faz rollback dos hooks criados se
houver falha. Só limpa um trampoline após remoção bem-sucedida: uma detour
ainda ativa precisa conservar sua função original. Readiness false impede
recibos de constantes em uma instalação parcial. A remoção/ativação ainda não
foi exercitada no jogo. Logs limitados são observações posteriores ao retorno
do SDK, não recibos de conclusão na GPU. Tags compartilham um produtor entre
features; nenhuma identidade de olho é inferida só do TLS.

Referência primária de assinaturas e identidade dos objetos:
[API oficial](https://github.com/NVIDIA-RTX/Streamline/blob/main/include/sl_core_api.h),
[tipos oficiais](https://github.com/NVIDIA-RTX/Streamline/blob/main/include/sl_core_types.h).
O layout nativo e os sites vêm da evidência local do executável identificado.

Validação: DLL compilada, 105/105 CTest em 11,68s; testes com ponteiros opacos
de 64 bits, quinta posição, arrays completos, erros, objetos inválidos e
campos distintos. Nenhuma execução no jogo, Quest ou medição de FPS.
Os relatórios `../artifacts/remastered-modern-streamline-call-owners.json`,
`remastered-modern-streamline-producers.json`, `remastered-modern-dlss-state-methods.json`,
`remastered-modern-dlss-method-rtti.json` e `remastered-modern-dlss-method-rip-owners.json`
permanecem exclusivamente locais. As duas últimas buscas não localizaram
receptores, sem provar sua inexistência. O candidato `01D2B640` não foi
confirmado como pipeline de DLSS e não recebe uma assinatura presumida.

Próximo: examinar o getter de opções e seu setter dinâmico, localizar o
receptor/rota real de avaliação e verificar IDs de viewport, limites e
históricos separados. Não usar `id | eye`: ids ímpares podem colidir. O gate
global e a reentrada moderna permanecem fechados até o port completo.

## Pipeline real, receptores e opções dinâmicas: continuação de 6 de outubro

As referências E8 brutas só foram promovidas após decodificação de instruções
com seus donos unwind e duas tabelas verificadas de nove entradas. A entrada
real é `01C02720`; as tabelas `01C035D0`/`01C035F4`, JMPs `01C027C5`/`01C028A8`,
reutilizam a definição da base em `01C02796`. O analisador acompanha aliases
parciais de registradores, chamadas e encontros dos caminhos conhecidos.
Chamadas invalidam bases voláteis; no ABI Windows x64, registradores salvos
pelo callee conservam a definição. Recalcula a prova após novos destinos;
um caminho que sobrescreve a base faz a análise falhar. Uma referência bruta,
fragmento isolado ou LEA para outro endereço não é prova de tabela.

O chamador verificado `01C07980` chama em `01C08489`, RETURN `01C0848E`, com
RCX=pipeline, RDX=descritor, R8D/R9D=índices, DWORD quinto na pilha e BYTE
sexto na pilha. Testa AL. O hook moderno preserva esses seis argumentos e o
resultado BYTE, sem a assinatura antiga de sete argumentos/float/retorno32.
Nenhum significado adicional dos três índices é presumido. O chamador ainda
tem JMP não resolvido `01C0887A`; não afirmar cobertura de todos os seus caminhos.
`01D2B640` permanece sem hook de DLSS e sem assinatura presumida.

Na tabela principal, modo6 entra em `01C034C3`. O getter virtual nativo usa
o objeto de `05A51950` e slot C0; seu resultado real vai para RSI. Os novos
hooks NÃO invocam esse getter por conta própria. Observam os receptores
passados aos métodos: preparo `01B78CE0`, CALL `01C034FC`, RETURN `01C03501`;
avaliação DLSS `01B77EB0`, CALL `01C0354B`, RETURN `01C03550`. A outra chamada
de preparo (RETURN `01C02890`) pertence a uma rota diferente e não recebe a
associação deste modo. RR segue CALL `01C03535`, RETURN `01C0353A`, e é excluído
deste escopo de DLSS comum pelo byte descritor+F73C. O modo vem de pipeline+74.

O preparo recebe três argumentos; a avaliação recebe cinco, com o índice
quinto na pilha. O chamador examinado ignora seus retornos. O encaminhador
conserva RAX inteiro como payload opaco: não interpreta bool/status/sucesso,
nem presume que eles tenham a mesma semântica de retorno do SDK. Estado+180
é o id real de viewport, lido em preparo, avaliação e builder. O escopo exige
o mesmo descritor, estado, contexto de recursos, C40/C44, id e par/geração/olho;
confere os índices 1 e 3 do pipeline na avaliação. Chamadas desconhecidas
suspendem a associação e restauram a anterior ao retornar. Uma divergência
na rota conhecida invalida o escopo. Tags aparecem no preparo E na avaliação.
Os logs podem associar a chamada CPU à rota, mas nunca publicam conclusão GPU.
O recibo de constantes também exige viewport igual ao estado nativo observado.

`slGetFeatureFunction` IAT `02984D18` tem ABI `(feature, name*, function**)`.
O getter de `slDLSSSetOptions` retorna em `01ED31B5`; o setter indireto de
duas referências `(viewport*, options*)` retorna em `01ED31CA`. O pacote
Options é versão3, GUID `6AC826E4-4C61-4101-A92D-638D421057B8`; só o header
32 bytes/next nulo é interpretado. Campos de opções/presets ficam intactos.
`slDLSSGetOptimalSettings` é outra função, não um setter de viewport.

O instalador do SDK agora exige quatro hooks em conjunto: constantes, tags,
avaliação e getter. O setter é ligado ao endereço retornado pelo SDK somente
se pertencer ao módulo sl.dlss.dll. Para a cache já preenchida, a rota nativa
de avaliação faz uma consulta ao getter original, após existir o dispositivo.
Isso intercepta o destino sem ler/escrever a cache `05DC00D0` nem trocar o
ponteiro que o jogo recebeu. Não chama o setter adicionalmente. Falha ou
troca de destino mantém readiness de opções false; retirada falha conserva
o trampoline necessário. Os três hooks nativos também são uma transação.
Os guards120/124/128, cache168..178 e cooldown17C não recebem escrita.

Assinaturas públicas conferidas na
[API Streamline](https://github.com/NVIDIA-RTX/Streamline/blob/main/include/sl_core_api.h)
e no [DLSS](https://github.com/NVIDIA-RTX/Streamline/blob/main/include/sl_dlss.h).
O getter é thread safe e exige dispositivo previamente definido; opções e
avaliação seguem as chamadas originais do jogo, sem introduzir execução paralela.
Os offsets e sites do jogo vêm do executável local identificado, não do SDK
atual sozinho. Nenhum cabeçalho de terceiros foi incorporado ao projeto.

Validação: DLL compilada, 105/105 CTest (11,71s), 50/50 Python e nova conferência
da entrada/chamador no executável. Logs `../artifacts/*modern-dlss-pipeline*20261006.log`.
Evidências `remastered-modern-dlss-pipeline-owner-evidence.json`,
`remastered-modern-dlss-pipeline-recheck.json`, `remastered-modern-dlss-options-getter-evidence.json`
e `remastered-modern-dlss-options-construction-evidence.json` permanecem locais.
O gate global rejeita 5.00c; nada instalado, nenhum teste de jogo/Quest/FPS.
Próximo: origem/alocação dos ids nativos em estado+180, liberação de recursos
por viewport e ciclo de vida/históricos com conclusão GPU. Não usar `id | eye`
nem ativar ids novos/reentrada sem essas provas. Demais hooks de câmera,
culling, efeitos e gameplay continuam pendentes para o objetivo completo.

## Estado compartilhado e Resource/ResourceTag modernos

O alocador chamado em `01B720AB` reserva190bytes; o construtor `01B7C940`
devolve self e `01B720C2` grava a global `0584DE48`. O getter leaf exato
`01BD9700` lê essa global. RTTI confirma o slotC0 da vtable primária de
CRenderInterface `036E03C8`. O leitor de estado agora compara em cada callback
a instância da global `05A51950`, a vtable, o slotC0, o ponteiro global e o
receptor real, além de self+16==1 e viewport self+180==0x5531D(348957).
Não chama o getter/construtor, não confunde comparação de ponteiros com posse
e não admite a tabela secundária nem receptor de outro estado.

O construtor usa `slInit` e passa SDKVersion `0x2000e0001fedc`:2.14.1,
magicfedc, conforme o empacotamento da
[versão Streamline](https://github.com/NVIDIA-RTX/Streamline/blob/main/include/sl_version.h).
Nunca chamar esse construtor para criar estado por olho. O teardown `01B74300`
chama slShutdown se self+16, libera a global compartilhada e a zera; a outra
rota `01B7C900` também chama slShutdown. A troca examinada em `01C121D0`
usa feature1000(FrameGeneration), não feature0(DLSS). Não extrapolar essas
duas chamadas para um ciclo completo de liberação por viewport. Free/Allocate
ausentes na IAT do jogo não provam ausência no componente dinâmico.

O produtor `01ED28F0` constrói Resource v1 GUID
`3A9D70CF-2418-4B72-8391-13F8721C7261`:112bytes, header32, tipoBYTE+20,
native*+28, memory*+30, view*+38, stateDWORD+40. ResourceTag v1 GUID
`4C6A5AAD-B445-496C-87FF-1AF3845BE653`:64bytes, resource*+20,
bufferTypeDWORD+28, lifecycleDWORD+2C, extent4DWORD+30. ABI também conferida
em [tipos públicos Streamline](https://github.com/NVIDIA-RTX/Streamline/blob/main/include/sl_core_types.h);
os stores e constantes do executável local estabelecem os offsets usados.
Não incorpora cabeçalhos de terceiros nem usa o layout legado.

`engine_dlss_resources.h` só interpreta header reconhecido/v1/next0 e Tex2D
com ponteiro válido, memory/view nulos e estado conhecido. Tags removidas
resource*=nullptr são eventos válidos de remoção, nunca recursos presentes.
Contagem1, lifecycle1(ValidUntilPresent) e quatro papéis0/1/3/4 vêm da rota
DLSS examinada. O produtor de avaliação usa estado0 para movimento e8(UAV)
para saída; profundidade/cor usam o estado obtido pelo rastreador nativo.

Uma lista CPU por avaliação exige a mesma identidade par/geração/olho,
descritor/estado/contexto, C40/C44, viewport, token OPACO e comando*. Observa
somente quatro tags bem sucedidas, sem repetições/remoções/extensões. Exige
origem0/0 e extensão positiva, três entradas do mesmo tamanho, saída não menor
e quatro endereços native distintos. Cada tag e avaliação incrementa um serial
global: atividade intermediária, inclusive em outra thread, recusa a lista.
Durante a avaliação a lista é retirada do TLS; atividade aninhada não pode
emprestar dados. Suspender renderização nativa aninhada invalida a lista do pai.
A tentativa consome a lista inclusive em erro; não há recibo reciclado.

O recibo resultante é **CPU apenas**. Não guarda estruturas nativas da pilha,
não dá AddRef, não desreferencia FrameToken/native* e não comprova geração da
lista de comandos, conteúdo da textura, posse ou GPU completion. Campos
`resource_ownership_verified` e `gpu_completion_verified` são false. Não o
envia ao mecanismo legado de completion nem libera recursos/ativação stereo.
Originais, arrays, argumentos e status continuam encaminhados intactos.

DLL compilada e106/106CTest em11,72s. Testes de recursos recusam mistura de
identidades, ponteiros, contadores, índices, comandos/tokens, atividade
intermediária, remoção, alias, tipos/versões/chains desconhecidos e status de
erro. Evidência `../artifacts/remastered-modern-dlss-resource-layout-evidence.json`
fica LOCAL. Gate5c e stereo_reentry_verified permanecem fechados; nenhum
teste de jogo/Quest/FPS ou instalação. Próximo: COM/device/queue ownership,
identidade moderna da lista e epochReset, submissão/fence e término GPU,
desligamento/reinit/troca do plugin antes de separar ids/históricos por olho.
Não presumir que o unwrap QI legado do Streamline esteja portado para2.14.1.

## Identidade moderna e referências COM próprias

`modern_dlss_ownership.h/.cpp` diferencia uma associação CPU de referências
COM adquiridas. Resolve somente endpoints nativos, recusando Unknown e
RenderDoc. Conserva um resultado com referências próprias de command list,
device, quatro Resources e suas identidades canônicas IUnknown. QI, SDK ou
cadeia inválida não devolvem uma posse parcial. ReShade usa a passagem de
interface previamente conferida. Nenhum campo privado de wrapper é lido.

Uma análise independente do componente instalado confirmou a classe
D3D12GraphicsCommandList: GUID `5B2662FB-EB28-4AEC-819E-1C1B4DE060F6`
em77938, a quinta classe de slGetNativeInterface, e a vtable782F0. Slots
QI23400/AddRef234C0/Release234F0/GetDevice23720/GetType23730/Close23740/
Reset23750. QI23400 compara as duas metades do marcador ADEC44E2...
em23435/23442 com778F0/778F8. O ramo correspondente devolve base com AddRef
e S_OK sem consultar estadoSDK. A busca anterior de LEA/MOV não encontrou
essas instruções CMP: era parcial, não prova de ausência. O fallback de
interfaces desconhecidas ainda tem salto indireto não resolvido234B0.

O verificador em memória exige tabela e slots exatos, os dois GUIDs, export
slGetNativeInterface7EA0 e SHA256 dos179bytes de QI23400..234B3:
`b3d0fde6f9ae0173c10dd926161a7e8aa9c74a810dec2a427ccddcdfaf5cac6f`.
Não guarda autorização entre unload/reinit; mudança/hook/corrupção falha
fechada. Só após essa verificação o resolver usa a QI do marcador nesta
classe moderna. Isso não extrapola o contrato a outros objetos Streamline.
Fontes públicas: [classe de comandos](https://github.com/NVIDIA-RTX/Streamline/blob/main/source/core/sl.interposer/d3d12/d3d12CommandList.h)
e [QI e Reset](https://github.com/NVIDIA-RTX/Streamline/blob/main/source/core/sl.interposer/d3d12/d3d12CommandList.cpp).
Evidência do binário efetivo permanece exclusivamente local em
`../artifacts/remastered-modern-streamline-command-base-evidence.json`.

A alternativa slGetNativeInterface tem dois argumentos e retorna referência
adicionada inclusive no fallback com o próprio input. Só é chamada com
capacidade explícita de inicialização/lifetime e serialização com o host;
o hook do jogo não fornece essa capacidade. Ref devolvida com erro é retirada,
self/ciclo/Unknown/depth excessiva recusados. Nunca usa a passagem genérica
legada que devolve Unknown intacto. A
[API oficial](https://github.com/NVIDIA-RTX/Streamline/blob/main/include/sl_core_api.h)
declara GetNativeInterface não thread safe. Os testes dessa alternativa
usam modelos IUnknown; não inicializam nem executam o SDK instalado.

Aquisição exige recibo CPU completo, viewport nativo5531D, DIRECT command
list e dispositivos nativos com mesma identidade IUnknown. Recursos com a
mesma identidade canônica são recusados. GetDesc deve ser Tex2D, formato
conhecido, tamanho suficiente para a extent, array1/mips presentes/sample1/
quality0; output exige ALLOW_UNORDERED_ACCESS. Referências não atestam estado
de barreiras ou conteúdo imutável. Token opaco e dados originais intactos.
O hook só faz aquisições temporárias em diagnósticos limitados; elas não
alimentam GPU tickets, completion/cache legado, reentrada ou históricos.

`fence_reached` exige destino positivo/finito e conclusão diferente de
UINT64_MAX. Esse valor sinaliza perda do dispositivo, conforme
[D3D12 GetCompletedValue](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12fence-getcompletedvalue).
Não transforma sucesso SDK, retorno de Execute ou AddRef em conclusãoGPU.

Probe opcional `w3vr_modern_dlss_ownership_gpu`: hardware RTX4070Ti, quatro
texturas reais, associações CPU fabricadas no laboratório. Confere alias,
extent insuficiente, ausênciaUAV, buffer em vez de Tex2D e dispositivo WARP
diferente. Executa24cópias GPU com Signal/Event/GetCompletedValue/readback;
solta refs do chamador e conserva as adquiridas até a fence. Reset só após
completion. Não carrega Streamline/ReShade/jogo e não valida DLSS/Quest/FPS.
Log exclusivamente local `../artifacts/modern-dlss-ownership-gpu-20261006.log`.
DLL compilada e107/107CTest em11,13s: resultados finais em
`../artifacts/modern-dlss-ownership-port-validation.json`. Gate5c fechado.
Próximo: recording epochs/Close/Execute/fence por endpoint, retenção até
GPU retirement e lifecycle do plugin antes de isolar ids/históricos por olho.

## Gravações modernas: identidade canônica e Reset antecipado

`acquire` agora também possui a identidade IUnknown canônica da command list.
`modern_dlss_recording.h` mantém essa referência e a do device, evitando
reaproveitamento do endereço enquanto o ledger existe. Não reutiliza as
chaves emprestadas do rastreador legado. A integração desse ledger aos hooks
reais permanece pendente; o diagnóstico moderno continua temporário.

Ledger começa desconhecido: requer observar Reset bem-sucedido antes de
admitir dados. Stamp deve ser capturado ANTES do produtor/SDK e comparado
depois; capturá-lo somente depois não detecta Reset durante a chamada.
Admissão exige stamp atual, Open, objeto/dispositivo iguais, identidade de
olho/par/geração válida e viewport nativo. Duplicatas e mais de64observações
são recusadas sem consumir a posse do chamador. Close bem-sucedido permite
take_closed uma única vez. ClosedRecording é móvel, não copiável e NÃO
significa execução ou conclusão GPU. Reset só abandona observações ainda
pendentes no ledger; pacotes já retirados mantêm suas próprias referências.
Reset falho não avança epoch e suspende admissão; Close falho torna o ledger
inválido permanentemente. Epoch não dá volta em UINT64_MAX.

O futuro adaptador precisa serializar produtor/Reset/Close/Execute e manter
os pacotes ANTES do Execute até a fence exata da queue/device. Destruição ou
atribuição de um pacote em voo, Signal falho, reenvio da mesma lista,
shutdown/reinit e perda do dispositivo ainda requerem política de retirement.
O ledger não chama nem intercepta essas APIs, não marca GPU completion e
não prova imutabilidade das texturas, estados de barreiras ou históricos.

Probe físico atualizado:24cópias, queue Wait numa fence de teste, Execute,
Signal e Reset imediato com allocator alternado ANTES de liberar a fence
pela CPU. O pacote anterior mantém texturas; stamp anterior é recusado.
Depois de Event/GetCompletedValue, readback confere todos os pixels úteis.
Refs das aquisições recusadas são soltas antes de testar a duração da posse.
Reset e Close inválidos são chamados em outra lista nativa e seus HRESULTs
reais recusados. Receipt/olho/par são fabricados, não resultam de DLSS.

A distinção entre Reset da lista e Reset do allocator segue a
[documentação oficial de Reset](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-reset):
a lista pode ser reaberta durante execução, mas o allocator em uso não pode
ser reutilizado antes do término. Falhas de
[Close](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-close)
não atestam uma gravação executável. DLL compilada e107/107CTest em11,81s.
Evidência nova LOCAL `../artifacts/modern-dlss-recording-port-validation.json`.
Nada instalado; gate5c fechado. Próximo: adaptador real de submissão e
retenção por queue/fence, incluindo falhas e ciclo de vida do plugin.

## Fila moderna: posse própria e dispositivo

`OwnedQueue` possui referências da interface nativa ID3D12CommandQueue,
IUnknown canônico da fila e device/identidade do device. `acquire_queue`
reutiliza o resolver estrito, recusa endpoints Unknown/RenderDoc, exige
DIRECT e dispositivo nativo. Em falha nenhuma posse parcial é devolvida.
`compatible_queue_device` confere SOMENTE compatibilidade de dispositivo:
mesmo device não significa mesma fila, envio observado ou GPU pronta.
O helper não chama Execute/Signal e não substitui a fila pelo swapchain.

`installed_streamline_queue_base` tem perfil separado do command-list:
confere tabela/slots/export/GUIDs e digest de toda a QI em memória, sem cache
positivo entre unload/reinit. Somente a classe de fila instalada exatamente
reconhecida recebe a capacidade de QI moderna. Nenhum campo privado do
wrapper é lido pelo mod. Alternativa SDK não thread safe permanece fechada
sem prova de inicialização e serialização. Gate5c fechado; helper ainda não
é adaptador de submissão/retirement do jogo.

A análise do SDK instalado confirmou o ramo de QI da fila com AddRef/base
e a conversão de arrays de command-list antes de Execute; fallbackQI,
destino virtual de Execute e helpers transitivos ainda não constituem prova
de execução nem conclusão GPU. Evidência nativa exclusivamente local em
`../artifacts/remastered-modern-streamline-queue-candidate.json`, reproduzida
por `../artifacts/inspect-modern-streamline-queue.py`. A
[classe pública da fila](https://github.com/NVIDIA-RTX/Streamline/blob/main/source/core/sl.interposer/d3d12/d3d12CommandQueue.h)
e [sua implementação](https://github.com/NVIDIA-RTX/Streamline/blob/main/source/core/sl.interposer/d3d12/d3d12CommandQueue.cpp)
servem de referência, não substituem a análise do binário efetivamente instalado.

Probe físico: duas filas DIRECT no mesmo device preservam identidades
diferentes; fila COMPUTE recusada; fila WARP em outro device adquirida mas
recusada como compatível com a avaliação da RTX. Referência original da fila
de teste é solta e24cópias usam somente OwnedQueue, com Reset antecipado,
fence e readback já descritos. Testes Streamline continuam estáticos/modelos;
probe só usa endpoints Native, nunca jogo/DLSS/Quest/FPS. DLL compilada,
107/107CTest em11,75s. Relatório LOCAL `modern-dlss-queue-port-validation.json`.
Próximo: observações serializadas Reset/Close/Execute, retenção por queue/fence
exata, Signal falho/reenvio/shutdown/device removal; depois IDs/históricos
por olho. Não ligar o pacote fechado ao completion/cache legado.
