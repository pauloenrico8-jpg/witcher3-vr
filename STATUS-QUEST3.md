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

## O que o código novo faz, passo a passo

1. Quando o mod base cria sua ligação com o sistema de VR, a parte nova pede
   acesso aos controles esquerdo e direito. OpenXR é a interface usada para
   conversar com o sistema de VR do PC.
2. A cada imagem, ela consulta a posição, a direção e, quando disponíveis, as
   velocidades de cada controle. Também lê gatilhos, botões e direcionais.
3. Ela mantém as duas mãos separadas. Se perder o acompanhamento de uma mão ou
   se o jogo deixar de receber os controles, descarta os dados antigos.
4. Ela disponibiliza esses dados para a próxima parte do projeto. Ainda falta
   usar os dados para desenhar as mãos, mover as armas e calcular contato,
   dano e bloqueio pela trajetória real da espada.
5. Existe uma função para solicitar vibração curta nos controles. Nenhuma
   colisão do jogo a utiliza ainda.

Foi escolhida a leitura das posições reais porque o pedido é de combate pela
trajetória da mão. O código novo não transforma um movimento em um botão de
ataque do jogo. Também não contém ainda o combate físico solicitado.

A configuração de exemplo solicita o modo do projeto base que produz imagens
para ambos os olhos, sem alternância entre olhos. É uma configuração para
desenvolvimento, não um perfil de desempenho validado. A leitura dos controles
fica desligada por padrão e pode ser solicitada em `[motion_controllers]`.
O bloqueio de incompatibilidade continua ativo com qualquer configuração.

## O que os testes comprovam

A compilação no Visual Studio 2026 terminou. Os 94 testes de software passaram.
O teste novo de controles usa um sistema de VR simulado: verifica mãos
independentes, leitura de botões, perda de acompanhamento, dados inválidos,
limites de vibração e encerramento dos recursos. Outro teste verifica a recusa
de imagens de programa alteradas ou truncadas.

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

## Trabalho que falta

- Adaptar e verificar os pontos internos de renderização e de jogo para 5.00c.
- Integrar as posições das mãos à aparência das mãos e armas do personagem.
- Integrar trajetória, contato, dano e bloqueio ao combate do jogo.
- Confirmar a ativação de mods e a abertura do jogo com a instalação limpa;
  conferir o complemento recomendado e possíveis conflitos.
- Validar no Quest 3 e medir Novigrad na máquina identificada.

O objetivo completo do pedido continua pendente. Não há garantia de 60 FPS nem
uma promessa de que basta instalar a DLL atual para jogar.
