// Leitura do CABECALHO de um Matroska, so para descobrir o idioma das faixas.
//
// POR QUE ISTO EXISTE. O pipeline da LG devolve, no sourceInfo, o idioma de
// cada faixa de AUDIO ("en", "es", "fr", "it") e NENHUM idioma de legenda:
// medido num arquivo do dono com 43 legendas, todas com
// "language":"(null)", e os unicos campos do subtitleTrackInfo sao trackNum,
// language, type e periodStart. Nao ha outro campo para ler — a informacao
// simplesmente nao sai do pipeline.
//
// O app web mostra os idiomas porque o NAVEGADOR demuxa o arquivo por conta
// propria e expoe textTracks. Este modulo faz a mesma coisa em pequeno: baixa
// os primeiros megabytes por Range e le o elemento Tracks do EBML.
//
// NAO E UM DEMUXER. Nao decodifica nada, nao segue Cues, nao le Clusters. Anda
// pela arvore de elementos ate Segment > Tracks e para. Qualquer coisa que nao
// case com o esperado faz a leitura desistir em silencio — o chamador continua
// com "Legenda N", que e o que havia antes.
#ifndef NV_MKV_H
#define NV_MKV_H

#define MKV_MAX_FAIXAS 64

typedef struct {
  int  numero;        // TrackNumber (NAO e o `trackNum` da LG: ver mkv_casar_legendas)
  int  tipo;          // 1 video, 2 audio, 17 legenda (TrackType do Matroska)
  char idioma[8];     // "por", "eng"... vazio quando o arquivo nao etiqueta
  char nome[48];      // Name, quando existe ("Forced", "SDH", "Full")
  char codec[24];     // CodecID ("S_TEXT/UTF8", "S_HDMV/PGS")
  // Registro de configuracao do Dolby Vision (dvcC/dvvC), do
  // BlockAdditionMapping da faixa de VIDEO. dvPerfil 0 = a faixa nao traz um.
  // Ver mkv_dv_decidir.
  int  dvPerfil;      // 5, 7, 8...
  int  dvCompat;      // bl_signal_compatibility_id: 1 = base HDR10 (o "8.1")
  int  dvRpu, dvEl, dvBl;   // camadas presentes: metadado, realce, base
} MkvFaixa;

#define MKV_MAX_CAPS 64

// CAPITULO. Existe pelo pos-reproducao: sem marcador, "quando comecam os
// creditos" vira chute, e um chute erra em minutos. Muitos lancamentos trazem
// um capitulo final chamado "End Credits"/"Creditos" — quando ele esta la, e a
// resposta exata, de graca, no cabecalho que ja baixamos para as faixas.
typedef struct {
  double inicio;      // segundos desde o inicio do arquivo
  char   nome[64];    // ChapString, quando o arquivo nomeia
} MkvCap;

// Le o cabecalho de `url` e preenche `saida`. Devolve quantas faixas achou, 0
// quando nao deu (nao e MKV, servidor sem Range, cabecalho maior que o trecho).
// BLOQUEIA: chamar de um fio proprio.
int mkv_faixas(const char *url, MkvFaixa *saida, int max);

// Mesma leitura, UMA viagem so, devolvendo tambem os capitulos. `caps` pode ser
// NULL. O numero de capitulos sai por `nCaps`.
//
// UMA VIAGEM E O PONTO: a nota no topo de mkv.c registra que esta leitura
// acontece com o video JA TOCANDO, pela mesma conexao — uma segunda descida de
// 320 KB para buscar capitulos custaria exatamente o engasgo que aquela nota
// descreve.
int mkv_faixas_e_caps(const char *url, MkvFaixa *saida, int max,
                      MkvCap *caps, int maxCaps, int *nCaps);

// Segundo do capitulo que se IDENTIFICA como creditos pelo nome, ou 0. Nao
// chuta pela posicao: quem sabe a duracao do filme e quem chama, e sem ela
// "ultimo capitulo" nao distingue creditos de cena final.
double mkv_creditos_nomeados(const MkvCap *caps, int n);

// CASA as legendas que a TV lista com as TrackEntry de legenda do arquivo (#92).
//
// O `trackNum` do subtitleTrackInfo da LG NAO e o TrackNumber do Matroska. E o
// ORDINAL da faixa entre as legendas do arquivo, contado de 0 na ordem das
// TrackEntry — o mesmo que o AVPlay da Samsung chama de track_num. MEDIDO na
// C9 (webOS 4.5) em 22/09/2026, num Erai-raws de One Piece com 8 legendas ASS:
// sourceInfo com trackNum 0..7, e as legendas do arquivo com TrackNumber
// 3..10 (1 e video, 2 e audio). TrackNumber 0 nem existe no formato. Casar
// por TrackNumber, como o video.c fazia, dava a legenda 0 a ninguem, a 1 ao
// VIDEO, a 2 ao AUDIO e da 3 em diante a legenda TRES posicoes antes — era o
// "Italian" do #92 mostrando falas em ingles, e o "English" sem o selo ASS
// caindo no desenho da TV, que pisca e come metade das falas.
//
// `tvNum[i]` e o trackNum da i-esima legenda da TV; `idxFx[i]` recebe o indice
// em `fx` da TrackEntry correspondente, ou -1. Devolve o modo usado:
//   MKV_CASA_ORDINAL  todos os trackNum cabem em [0, legendas do arquivo) e as
//                     duas contagens batem — o caso medido;
//   MKV_CASA_NUMERO   todo trackNum e o TrackNumber de uma legenda do arquivo
//                     (a leitura antiga; fica como segunda tentativa, nunca
//                     misturada com a primeira);
//   MKV_CASA_NADA     nenhuma das duas fecha: nada e casado. Idioma errado e
//                     pior que idioma nenhum, e codec errado manda a faixa
//                     errada para o overlay.
enum { MKV_CASA_NADA = 0, MKV_CASA_ORDINAL = 1, MKV_CASA_NUMERO = 2 };
int mkv_casar_legendas(const MkvFaixa *fx, int n, const int *tvNum, int nTv,
                       int *idxFx);

// --- DOLBY VISION: declarar ou nao, decidido pelo ARQUIVO --------------------
//
// O webOS 4 nao le o registro de DV de um MKV, entao o app tem de dizer o
// perfil no load (DolbyHdrInfo, ver video.c). MEDIDO na C9: declarar o perfil
// errado nao falha, fica SEM IMAGEM com o audio correndo, e o uMS nao da sinal
// nenhum disso. Por isso a declaracao sai do registro dvcC/dvvC que o proprio
// arquivo carrega, e so nos casos em que ha uma camada so:
//
//   MKV_DV_DECLARAR  perfil 5, ou 8 com base HDR10 (8.1), sem camada de
//                    realce: declarar `perfil` / "single".
//   MKV_DV_RECUSAR   o arquivo tem DV, mas num formato que o modo URI nao
//                    entrega ao decoder (perfil 7 com camada de realce no mesmo
//                    fluxo, 8.2/8.4, 4...). Tocar como HDR10: com imagem.
//   MKV_DV_SEM       o cabecalho foi lido e nao ha DV. O rotulo do addon mentiu.
//
// Puro: sem rede, sem estado — tests/mkv_dv.sh exercita.
enum { MKV_DV_SEM = 0, MKV_DV_DECLARAR = 1, MKV_DV_RECUSAR = 2 };
int mkv_dv_decidir(const MkvFaixa *fx, int n, int *perfil);

#endif
