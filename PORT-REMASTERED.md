# Adaptação para Remastered 5.00c — evidências de renderização

Estado em 06/10/2026: análise estática e início da adaptação do código de câmera,
sem liberar o mod no jogo.
“Estática” significa ler o programa como um arquivo, sem executar suas funções.
O objetivo permanece VR com imagens novas nos dois olhos, mãos e armas livres,
combate físico e pelo menos 60 FPS reais em Novigrad.

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

O próximo passo é adaptar a preparação do produtor e os demais campos de
câmera, constantes de renderização e jitter (o pequeno deslocamento usado
pela reconstrução temporal). O produtor chama Engine slot 13 em `022758EB`,
com engine global `05A518F8` e frame em RDX; a rotina `0224C600` altera campos
do frame antes do comando. Há ainda uma etapa opcional do objeto CGame+100 e
uma rota opcional anterior à criação do comando que precisam ser revisadas.
O novo envio de comandos ainda não reproduz essas etapas. Não liberar o
bloqueio por contratos isolados; duração dos recursos e imagens reais precisam
de verificação em execução.
