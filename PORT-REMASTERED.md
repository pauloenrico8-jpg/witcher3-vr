# Adaptação para Remastered 5.00c — evidências de renderização

Estado em 06/10/2026: análise estática, sem liberar o mod no jogo.
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
| Construção de `CRenderFrame` | `0x01B80550` instala a tabela `0x03958958` e aloca `0xF910` bytes | Essa função participa da criação dos dados de renderização; não foi provada como substituta automática do hook antigo da fábrica. |
| Cópia/reconstrução de câmera em `0x0228A970` | Chama `0x0228AB40` e preserva a fórmula de escala de distância com coeficiente `3.000002861022949` | Candidato forte para comparar com `kEngineViewCopyRebuildRva`; parâmetros e todos os campos ainda exigem revisão. |
| Registro temporal da câmera | A cópia em `0x0228A970` desloca origem e destino até `+0x530` antes de copiar o registro | O código 4.04 usa `+0x460`; mudar apenas o endereço da função conservaria um acesso incompatível. |
| Cópia do descritor em `0x00324430` | Há leitura de um campo em `+0xF748` | O prefixo antigo de `0xC000` bytes não cobre todos os campos observados desse objeto. |
| Construtor temporal em `0x02288F00` | Chamador `0x022B6337` prepara tempo, posição, orientação e parâmetros de projeção; resultado ocupa campos até `+0xAC` | Candidato para comparar com o construtor temporal de nove argumentos do código antigo. |

Os endereços da tabela são relativos ao início do executável, chamados RVA.
Eles são específicos do SHA-256 registrado. Nenhum foi usado para ativar um hook,
isto é, uma ligação que intercepta uma função do jogo enquanto ela executa.

Os 15 registros de nomes/callbacks do relatório anterior foram reanalisados com
o novo tratamento de blocos e fluxo. Seus candidatos permaneceram iguais.
Registro local: `../artifacts/remastered-callback-controlflow-evidence.json`.

## Limites que continuam abertos

- As tabelas identificam classes, mas não provam todos os campos, argumentos,
  chamadores e condições usados pelo mod.
- Os grafos têm profundidade limitada. Destinos de tabelas de desvios e chamadas
  virtuais precisam de análise adicional; não são inferidos automaticamente.
- O callback de script `GetCurrentViewportResolution` continua distinto do hook
  nativo de resolução que recebe quatro argumentos no projeto original.
- Ainda faltam as rotas de constantes de renderização, históricos temporais,
  fábrica correta, passagem dos dois olhos, carregamento e proprietários dos
  objetos. A DLL continua recusando a versão incompatível.
- Não há execução no Quest, integração de combate físico ou medição de Novigrad.

## Validação e reprodução

`python tests/remastered_analysis_tests.py -v` passou em 22 testes independentes.
Eles verificam blocos separados, alinhamento dos registros, cadeias inválidas,
arquivos truncados, tabelas de classes, classes com nomes de callback iguais,
falsas chamadas dentro de dados ou depois de um retorno, e a recusa de caminhos
de saída que sobrescreveriam o executável ou o relatório de entrada.
O registro é `../artifacts/remastered-analysis-tests-20261006.log`.
Esses testes não substituem os 95 testes C++ registrados anteriormente nem
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

O próximo passo é revisar o contrato das funções candidatas e os campos da
câmera/descritor antes de adaptar os acessos do código. Não liberar o bloqueio
da versão apenas porque uma função foi localizada.
