%This work is licensed under the
%Creative Commons Attribution-Share Alike 3.0 United States License.
%To view a copy of this license, visit
%http://creativecommons.org/licenses/by-sa/3.0/us/ or send a letter to
%Creative Commons,
%171 Second Street, Suite 300,
%San Francisco, California, 94105, USA.

\documentclass[PAPERSIZE]{scrbook}
\setlength{\pdfpagewidth}{\paperwidth}
\setlength{\pdfpageheight}{\paperheight}
\setlength{\textwidth}{6in}
\usepackage{amsmath}
\usepackage{graphicx}
\usepackage{picins}
\usepackage{fancyvrb}
\usepackage{relsize}
\usepackage{array}
\usepackage{wrapfig}
\usepackage{subfig}
\usepackage{multicol}
\usepackage{paralist}
\usepackage{textcomp}
\usepackage{fancyvrb}
\usepackage{multirow}
\usepackage{rotating}
\usepackage[toc,page]{appendix}
\usepackage{hyperref}
\newcommand{\xor}{\textbf{ xor }}
%#1 = i
%#2 = byte
%#3 = previous checksum
%#4 = shift results
%#5 = new xor
%#6 = new CRC-16
\newcommand{\CRCSIXTEEN}[6]{\text{checksum}_{#1} &= \text{CRC16}(\texttt{#2}\xor(\texttt{#3} \gg \texttt{8}))\xor(\texttt{#3} \ll \texttt{8}) = \text{CRC16}(\texttt{#4})\xor \texttt{#5} = \texttt{#6}}
\newcommand{\LINK}[1]{\href{#1}{\texttt{#1}}}
\newcommand{\REFERENCE}[2]{\item #1 \\ \LINK{#2}}
\newcommand{\VAR}[1]{``{#1}''}
\newcommand{\ATOM}[1]{\texttt{#1}}
\newcommand{\SAMPLE}[0]{\text{Sample}}
\newcommand{\RESIDUAL}[0]{\text{Residual}}
\newcommand{\WARMUP}[0]{\text{Warm Up}}
\newcommand{\COEFF}[0]{\text{LPC Coefficient}}
\newcommand{\SCOEFF}[0]{C}
\newcommand{\SSAMPLE}[0]{S}
\title{Audio Formats Reference}
\author{Brian Langenberger}
\begin{document}
\maketitle
\tableofcontents
\include{introduction}
\include{basics}
\include{wav}
\include{aiff}
\include{au}
\include{shorten}
\include{flac}
\include{wavpack}
\include{ape}
\include{mp3}
\include{m4a}
\include{alac}
\include{vorbis}
\include{oggflac}
\include{speex}
\include{musepack}
\include{freedb}
\include{musicbrainz}
\include{musicbrainz_mmd}
\include{replaygain}
\begin{appendices}
\include{references}
\include{license}
\end{appendices}
\end{document}
