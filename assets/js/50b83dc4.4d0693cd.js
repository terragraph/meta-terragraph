"use strict";(self.webpackChunkwebsite=self.webpackChunkwebsite||[]).push([[9831],{3905:function(e,n,t){t.d(n,{Zo:function(){return d},kt:function(){return u}});var a=t(7294);function r(e,n,t){return n in e?Object.defineProperty(e,n,{value:t,enumerable:!0,configurable:!0,writable:!0}):e[n]=t,e}function i(e,n){var t=Object.keys(e);if(Object.getOwnPropertySymbols){var a=Object.getOwnPropertySymbols(e);n&&(a=a.filter((function(n){return Object.getOwnPropertyDescriptor(e,n).enumerable}))),t.push.apply(t,a)}return t}function o(e){for(var n=1;n<arguments.length;n++){var t=null!=arguments[n]?arguments[n]:{};n%2?i(Object(t),!0).forEach((function(n){r(e,n,t[n])})):Object.getOwnPropertyDescriptors?Object.defineProperties(e,Object.getOwnPropertyDescriptors(t)):i(Object(t)).forEach((function(n){Object.defineProperty(e,n,Object.getOwnPropertyDescriptor(t,n))}))}return e}function l(e,n){if(null==e)return{};var t,a,r=function(e,n){if(null==e)return{};var t,a,r={},i=Object.keys(e);for(a=0;a<i.length;a++)t=i[a],n.indexOf(t)>=0||(r[t]=e[t]);return r}(e,n);if(Object.getOwnPropertySymbols){var i=Object.getOwnPropertySymbols(e);for(a=0;a<i.length;a++)t=i[a],n.indexOf(t)>=0||Object.prototype.propertyIsEnumerable.call(e,t)&&(r[t]=e[t])}return r}var s=a.createContext({}),p=function(e){var n=a.useContext(s),t=n;return e&&(t="function"==typeof e?e(n):o(o({},n),e)),t},d=function(e){var n=p(e.components);return a.createElement(s.Provider,{value:n},e.children)},c={inlineCode:"code",wrapper:function(e){var n=e.children;return a.createElement(a.Fragment,{},n)}},m=a.forwardRef((function(e,n){var t=e.components,r=e.mdxType,i=e.originalType,s=e.parentName,d=l(e,["components","mdxType","originalType","parentName"]),m=p(t),u=r,h=m["".concat(s,".").concat(u)]||m[u]||c[u]||i;return t?a.createElement(h,o(o({ref:n},d),{},{components:t})):a.createElement(h,o({ref:n},d))}));function u(e,n){var t=arguments,r=n&&n.mdxType;if("string"==typeof e||r){var i=t.length,o=new Array(i);o[0]=m;var l={};for(var s in n)hasOwnProperty.call(n,s)&&(l[s]=n[s]);l.originalType=e,l.mdxType="string"==typeof e?e:r,o[1]=l;for(var p=2;p<i;p++)o[p]=t[p];return a.createElement.apply(null,o)}return a.createElement.apply(null,t)}m.displayName="MDXCreateElement"},5684:function(e,n,t){t.r(n),t.d(n,{assets:function(){return d},contentTitle:function(){return s},default:function(){return u},frontMatter:function(){return l},metadata:function(){return p},toc:function(){return c}});var a=t(3117),r=t(102),i=(t(7294),t(3905)),o=["components"],l={},s="Scans",p={unversionedId:"developer/Scans",id:"developer/Scans",title:"Scans",description:"This document describes the architecture for scans in Terragraph.",source:"@site/../docs/developer/Scans.md",sourceDirName:"developer",slug:"/developer/Scans",permalink:"/docs/developer/Scans",draft:!1,editUrl:"https://github.com/terragraph/meta-terragraph/edit/main/docs/../docs/developer/Scans.md",tags:[],version:"current",frontMatter:{},sidebar:"developerManualSidebar",previous:{title:"Configuration Management",permalink:"/docs/developer/Configuration_Management"},next:{title:"Network Measurements",permalink:"/docs/developer/Network_Measurements"}},d={},c=[{value:"Overview",id:"overview",level:2},{value:"Time Units",id:"time-units",level:3},{value:"Scheduling",id:"scheduling",level:3},{value:"Message Exchange",id:"message-exchange",level:3},{value:"Scan Results",id:"scan-results",level:3},{value:"RF State",id:"rf-state",level:3},{value:"Topology Scan",id:"topology-scan",level:2},{value:"Broadcast Beamforming Procedure",id:"broadcast-beamforming-procedure",level:3},{value:"Training Packet Formats",id:"training-packet-formats",level:3},{value:"Usage",id:"usage",level:3}],m={toc:c};function u(e){var n=e.components,t=(0,r.Z)(e,o);return(0,i.kt)("wrapper",(0,a.Z)({},m,t,{components:n,mdxType:"MDXLayout"}),(0,i.kt)("h1",{id:"scans"},"Scans"),(0,i.kt)("p",null,"This document describes the architecture for scans in Terragraph."),(0,i.kt)("h2",{id:"overview"},"Overview"),(0,i.kt)("p",null,(0,i.kt)("inlineCode",{parentName:"p"},"ScanApp")," is responsible for initiating scans on nodes and collecting the\nmeasurement results. Scans are scheduled by the controller to run periodically\nand in parallel, using a graph coloring algorithm in ",(0,i.kt)("inlineCode",{parentName:"p"},"ScanScheduler")," and a slot\nscheduling mechanism in ",(0,i.kt)("inlineCode",{parentName:"p"},"SchedulerApp"),". The minion simply passes controller\ncommands to the driver, and returns results from the driver to the controller."),(0,i.kt)("p",null,"There are several scan types, defined in the Thrift enum ",(0,i.kt)("inlineCode",{parentName:"p"},"thrift::ScanType"),".\nThese are listed in the table below."),(0,i.kt)("table",null,(0,i.kt)("thead",{parentName:"table"},(0,i.kt)("tr",{parentName:"thead"},(0,i.kt)("th",{parentName:"tr",align:null},"Scan"),(0,i.kt)("th",{parentName:"tr",align:null},"Thrift Type"))),(0,i.kt)("tbody",{parentName:"table"},(0,i.kt)("tr",{parentName:"tbody"},(0,i.kt)("td",{parentName:"tr",align:null},"Initial Beamforming"),(0,i.kt)("td",{parentName:"tr",align:null},"n/a")),(0,i.kt)("tr",{parentName:"tbody"},(0,i.kt)("td",{parentName:"tr",align:null},"Periodic Beamforming"),(0,i.kt)("td",{parentName:"tr",align:null},(0,i.kt)("inlineCode",{parentName:"td"},"PBF"))),(0,i.kt)("tr",{parentName:"tbody"},(0,i.kt)("td",{parentName:"tr",align:null},"Interference Measurement"),(0,i.kt)("td",{parentName:"tr",align:null},(0,i.kt)("inlineCode",{parentName:"td"},"IM"))),(0,i.kt)("tr",{parentName:"tbody"},(0,i.kt)("td",{parentName:"tr",align:null},"Runtime Calibration"),(0,i.kt)("td",{parentName:"tr",align:null},(0,i.kt)("inlineCode",{parentName:"td"},"RTCAL"))),(0,i.kt)("tr",{parentName:"tbody"},(0,i.kt)("td",{parentName:"tr",align:null},"Coordinated Beamforming"),(0,i.kt)("td",{parentName:"tr",align:null},(0,i.kt)("inlineCode",{parentName:"td"},"CBF_TX"),", ",(0,i.kt)("inlineCode",{parentName:"td"},"CBF_RX"))),(0,i.kt)("tr",{parentName:"tbody"},(0,i.kt)("td",{parentName:"tr",align:null},"Topology Scan"),(0,i.kt)("td",{parentName:"tr",align:null},(0,i.kt)("inlineCode",{parentName:"td"},"TOPO"))))),(0,i.kt)("p",null,"Note that QTI firmware does ",(0,i.kt)("strong",{parentName:"p"},"not")," support RTCAL and CBF scans."),(0,i.kt)("h3",{id:"time-units"},"Time Units"),(0,i.kt)("p",null,"The time-related terminology used for scan scheduling are defined in the table\nbelow."),(0,i.kt)("table",null,(0,i.kt)("thead",{parentName:"table"},(0,i.kt)("tr",{parentName:"thead"},(0,i.kt)("th",{parentName:"tr",align:null},"Term"),(0,i.kt)("th",{parentName:"tr",align:null},"Description"))),(0,i.kt)("tbody",{parentName:"table"},(0,i.kt)("tr",{parentName:"tbody"},(0,i.kt)("td",{parentName:"tr",align:null},"UNIX Epoch Time"),(0,i.kt)("td",{parentName:"tr",align:null},"Time that has elapsed since 1 January 1970, ",(0,i.kt)("em",{parentName:"td"},"minus")," leap seconds")),(0,i.kt)("tr",{parentName:"tbody"},(0,i.kt)("td",{parentName:"tr",align:null},"GPS Epoch Time"),(0,i.kt)("td",{parentName:"tr",align:null},"Time that has elapsed since 6 January 1980, ",(0,i.kt)("em",{parentName:"td"},"ignoring")," leap seconds")),(0,i.kt)("tr",{parentName:"tbody"},(0,i.kt)("td",{parentName:"tr",align:null},"Superframe"),(0,i.kt)("td",{parentName:"tr",align:null},"1.6ms (4 frames)")),(0,i.kt)("tr",{parentName:"tbody"},(0,i.kt)("td",{parentName:"tr",align:null},"BWGD (Bandwidth Grant Duration)"),(0,i.kt)("td",{parentName:"tr",align:null},"25.6ms (16 superframes)")),(0,i.kt)("tr",{parentName:"tbody"},(0,i.kt)("td",{parentName:"tr",align:null},"BWGD Index"),(0,i.kt)("td",{parentName:"tr",align:null},"Integer index of a BWGD interval since GPS epoch")))),(0,i.kt)("p",null,"The controller must convert between its system time (based on UNIX epoch) and\nfirmware time (based on GPS epoch). There are some utility functions for time\nconversions in ",(0,i.kt)("inlineCode",{parentName:"p"},"ScanScheduler"),"."),(0,i.kt)("h3",{id:"scheduling"},"Scheduling"),(0,i.kt)("p",null,"Scans scheduled over the entire network are referred to as a ",(0,i.kt)("em",{parentName:"p"},"scan group"),". For\nperiodic scans, a single scan group can contain scans of different types."),(0,i.kt)("p",null,"To prevent collisions and ensure proper ordering, a frame structure is defined\nwith 16 BWGDs in a slot and 128 slots (both configurable) in a period. Using\nthese default values, the period lasts about 52.4 seconds. The 128 slots are\nallocated to scan types as shown in the table below:"),(0,i.kt)("table",null,(0,i.kt)("thead",{parentName:"table"},(0,i.kt)("tr",{parentName:"thead"},(0,i.kt)("th",{parentName:"tr",align:null},"Scan Type"),(0,i.kt)("th",{parentName:"tr",align:null},"Start Slot #"),(0,i.kt)("th",{parentName:"tr",align:null},"Duration (slots)"))),(0,i.kt)("tbody",{parentName:"table"},(0,i.kt)("tr",{parentName:"tbody"},(0,i.kt)("td",{parentName:"tr",align:null},(0,i.kt)("inlineCode",{parentName:"td"},"PBF")),(0,i.kt)("td",{parentName:"tr",align:null},"13, 77"),(0,i.kt)("td",{parentName:"tr",align:null},"5")),(0,i.kt)("tr",{parentName:"tbody"},(0,i.kt)("td",{parentName:"tr",align:null},"Hybrid ",(0,i.kt)("inlineCode",{parentName:"td"},"PBF")),(0,i.kt)("td",{parentName:"tr",align:null},"13, 77"),(0,i.kt)("td",{parentName:"tr",align:null},"10")),(0,i.kt)("tr",{parentName:"tbody"},(0,i.kt)("td",{parentName:"tr",align:null},(0,i.kt)("inlineCode",{parentName:"td"},"IM")),(0,i.kt)("td",{parentName:"tr",align:null},"0, 64"),(0,i.kt)("td",{parentName:"tr",align:null},"5")),(0,i.kt)("tr",{parentName:"tbody"},(0,i.kt)("td",{parentName:"tr",align:null},(0,i.kt)("inlineCode",{parentName:"td"},"RTCAL")),(0,i.kt)("td",{parentName:"tr",align:null},"25, 28, 31, 34, 89, 92, 95, 98"),(0,i.kt)("td",{parentName:"tr",align:null},"2")),(0,i.kt)("tr",{parentName:"tbody"},(0,i.kt)("td",{parentName:"tr",align:null},(0,i.kt)("inlineCode",{parentName:"td"},"CBF_TX"),", ",(0,i.kt)("inlineCode",{parentName:"td"},"CBF_RX")),(0,i.kt)("td",{parentName:"tr",align:null},"38, 102"),(0,i.kt)("td",{parentName:"tr",align:null},"5")),(0,i.kt)("tr",{parentName:"tbody"},(0,i.kt)("td",{parentName:"tr",align:null},(0,i.kt)("inlineCode",{parentName:"td"},"CBF_TX"),", ",(0,i.kt)("inlineCode",{parentName:"td"},"CBF_RX")," Apply"),(0,i.kt)("td",{parentName:"tr",align:null},"58, 122"),(0,i.kt)("td",{parentName:"tr",align:null},"1")),(0,i.kt)("tr",{parentName:"tbody"},(0,i.kt)("td",{parentName:"tr",align:null},(0,i.kt)("inlineCode",{parentName:"td"},"TOPO")),(0,i.kt)("td",{parentName:"tr",align:null},"n/a"),(0,i.kt)("td",{parentName:"tr",align:null},"-")))),(0,i.kt)("p",null,"For example, 5 slots (about 2 seconds) are reserved for ",(0,i.kt)("inlineCode",{parentName:"p"},"IM")," scans starting at\nslots 0 and 64."),(0,i.kt)("p",null,"An algorithm determines which nodes go in which slots. The algorithm avoids\nputting interfering nodes in the same start slot. Depending on the particular\ntopology of the network, a scan group can take minutes or hours to complete."),(0,i.kt)("p",null,"Ordering among scans are guaranteed by the controller. ",(0,i.kt)("inlineCode",{parentName:"p"},"PBF"),", ",(0,i.kt)("inlineCode",{parentName:"p"},"RTCAL"),", and ",(0,i.kt)("inlineCode",{parentName:"p"},"CBF"),"\nscans will always be scheduled in that order. There is no ordering between ",(0,i.kt)("inlineCode",{parentName:"p"},"IM"),"\nscans and other scan types. If a pair of nodes is scheduled for ",(0,i.kt)("inlineCode",{parentName:"p"},"PBF")," and\n",(0,i.kt)("inlineCode",{parentName:"p"},"RTCAL")," scans, for example, then the slot allocated for ",(0,i.kt)("inlineCode",{parentName:"p"},"PBF")," will come before\nthe slots allocated for ",(0,i.kt)("inlineCode",{parentName:"p"},"RTCAL")," (",(0,i.kt)("inlineCode",{parentName:"p"},"RTCAL")," requires two scans per link direction\nwhereas one ",(0,i.kt)("inlineCode",{parentName:"p"},"PBF")," scan refines both the TX and RX beams in that link direction)."),(0,i.kt)("p",null,"When ",(0,i.kt)("inlineCode",{parentName:"p"},"RTCAL")," runs as part of periodic scans, RX VBS gets enabled by a series of\nthree consecutive ",(0,i.kt)("inlineCode",{parentName:"p"},"RTCAL")," procedures indicated by the scan subtypes\n",(0,i.kt)("inlineCode",{parentName:"p"},"TOP_RX_CAL"),", ",(0,i.kt)("inlineCode",{parentName:"p"},"BOT_RX_CAL"),", and ",(0,i.kt)("inlineCode",{parentName:"p"},"VBS_RX_CAL"),"."),(0,i.kt)("p",null,"A scheduled scan uses relative PBF (3x3 grid) and fine IM scans (scans all\nazimuth beams) by default. To run a ",(0,i.kt)("inlineCode",{parentName:"p"},"PBF")," scan every 4 hours, for example, set\n",(0,i.kt)("inlineCode",{parentName:"p"},"ScanSchedule")," as shown below. Note that the first scan in the schedule will\nstart ",(0,i.kt)("inlineCode",{parentName:"p"},"combinedScanTimeoutSec")," seconds after issuing the command."),(0,i.kt)("pre",null,(0,i.kt)("code",{parentName:"pre",className:"language-json"},'{\n  "combinedScanTimeoutSec": 240,\n  "pbfEnable": true,\n  "rtcalEnable": false,\n  "cbfEnable": false,\n  "imEnable": false\n}\n')),(0,i.kt)("h3",{id:"message-exchange"},"Message Exchange"),(0,i.kt)("p",null,"Scans are initiated by the controller when receiving a ",(0,i.kt)("inlineCode",{parentName:"p"},"StartScan")," message from\nany the following sources:"),(0,i.kt)("ul",null,(0,i.kt)("li",{parentName:"ul"},"Automatic scan scheduler within ",(0,i.kt)("inlineCode",{parentName:"li"},"ScanApp")),(0,i.kt)("li",{parentName:"ul"},"Topology scan scheduler within ",(0,i.kt)("inlineCode",{parentName:"li"},"TopologyBuilderApp")),(0,i.kt)("li",{parentName:"ul"},"Direct user command (e.g. via TG CLI)")),(0,i.kt)("p",null,"The controller then sends a ",(0,i.kt)("inlineCode",{parentName:"p"},"ScanReq")," message to each minion participating in\nthe scan; minions use the pass-through framework to forward these requests to\nthe firmware. When complete, each scan response is sent back to the controller\nin a ",(0,i.kt)("inlineCode",{parentName:"p"},"ScanResp")," message. The controller stores all scan results in a\n",(0,i.kt)("inlineCode",{parentName:"p"},"ScanStatus")," structure, which maps scan IDs to ",(0,i.kt)("inlineCode",{parentName:"p"},"ScanData")," structures."),(0,i.kt)("p",null,"For manual scans, nodes can be specified using a node-wide identifier (i.e. name\nor node MAC address) or a radio MAC address. The former is accepted for\nbackward-compatibility reasons on single-radio nodes only (see\n",(0,i.kt)("inlineCode",{parentName:"p"},"ScanData::convertMacToName")," and related code)."),(0,i.kt)("h3",{id:"scan-results"},"Scan Results"),(0,i.kt)("p",null,'When an individual scan is scheduled, it is assigned a "token" (',(0,i.kt)("inlineCode",{parentName:"p"},"scanId"),"). Scan\nresponses are not guaranteed to come back from the nodes in order. When\nresponses are received from nodes, they are assigned a response ID (",(0,i.kt)("inlineCode",{parentName:"p"},"respId"),")\nthat will always be in the order the responses are received."),(0,i.kt)("p",null,"Scan results can be queried through a ",(0,i.kt)("inlineCode",{parentName:"p"},"GetScanStatus")," request to the controller,\nand are available once all nodes involved in a scan have sent a response. There\ncan be up to two measurements per frame (referring to the slotmap figure above).\nDepending on the distance of the link and the geometry, beam combinations for\nwhich the SNR is below the sensitivity (around -10dB) will not be reported."),(0,i.kt)("pre",null,(0,i.kt)("code",{parentName:"pre",className:"language-bash"},'$ tg scan status -f json  # (or \'raw\' or \'table\')\n{\n  "scans": {\n    "1": {\n      "responses": {\n        "box1": {\n          "token": 1,\n          ...\n        },\n        "box2": {\n          "token": 1,\n          "routeInfoList": [\n            {\n              "route": {\n                "tx": 0,\n                "rx": 0\n              },\n              "rssi": -10.0,\n              "snrEst": 27.5,\n              "postSnr": 18.0,\n              "rxStart": 4104,\n              "packetIdx": 0,\n              "sweepIdx": 0\n            },\n            ...\n          ],\n          ...\n        }\n      },\n      ...\n    }\n  }\n}\n')),(0,i.kt)("p",null,"The ",(0,i.kt)("inlineCode",{parentName:"p"},"isConcise")," flag suppresses the ",(0,i.kt)("inlineCode",{parentName:"p"},"routeInfoList")," in the structure above,\nwhich can be very large as it contains the SNR, RSSI, and other information for\nevery measurement taken in the scan."),(0,i.kt)("pre",null,(0,i.kt)("code",{parentName:"pre",className:"language-bash"},"$ tg scan status --concise\nScan Id 1, tx node box1, rx node box1, start bwgd 47044495072, response bwgd 47044495142, tx power 1\nScan Id 1, tx node box1, rx node box2, start bwgd 47044495072, response bwgd 47044495140\n")),(0,i.kt)("h3",{id:"rf-state"},"RF State"),(0,i.kt)("p",null,(0,i.kt)("inlineCode",{parentName:"p"},"ScanApp")," stores processed information from periodic scans in a local ",(0,i.kt)("inlineCode",{parentName:"p"},"rfState_"),"\nvariable. The purpose of ",(0,i.kt)("inlineCode",{parentName:"p"},"rfState_")," is not to maintain historical scan results,\nbut to have an up-to-date snapshot of the network's state in terms of RF\ncoupling between each sector for all possible beam combinations. ",(0,i.kt)("inlineCode",{parentName:"p"},"rfState_"),"\nstores averaged routes from fine and relative ",(0,i.kt)("inlineCode",{parentName:"p"},"IM")," scan results and the latest\ndirectional beam for each link as indicated in ",(0,i.kt)("inlineCode",{parentName:"p"},"PBF")," scan results. ",(0,i.kt)("inlineCode",{parentName:"p"},"ScanApp"),"\nuses ",(0,i.kt)("inlineCode",{parentName:"p"},"rfState_")," to generate the CBF configuration and to adjust LA/TPC\nparameters (currently ",(0,i.kt)("inlineCode",{parentName:"p"},"laMaxMcs"),") based on cross-link coupling. It can also be\nused for coloring algorithms that rely on RF connectivity between nodes (e.g.\nassignment of polarity, Golay, ignition slots, etc.)."),(0,i.kt)("p",null,"Because ",(0,i.kt)("inlineCode",{parentName:"p"},"rfState_")," contains all information needed for various controller\nalgorithms and can be easily exported to or imported from a file, ",(0,i.kt)("inlineCode",{parentName:"p"},"rfState_")," is\na useful way to unit-test controller algorithm changes, debug new issues, and\nperform system studies."),(0,i.kt)("h2",{id:"topology-scan"},"Topology Scan"),(0,i.kt)("p",null,"The goal of running topology scans is to discover nodes in network without\nknowing their MAC addresses or GPS positions in advance. Topology scans make\nuse of a broadcast beamforming (BF) protocol to collect information from nearby\nnodes."),(0,i.kt)("p",null,"Topology scans are the main piece of the\n",(0,i.kt)("a",{parentName:"p",href:"/docs/developer/Topology_Discovery"},"Topology Discovery")," algorithm."),(0,i.kt)("h3",{id:"broadcast-beamforming-procedure"},"Broadcast Beamforming Procedure"),(0,i.kt)("p",null,"The broadcast beamforming protocol closely resembles initial beamforming. Two\npacket types are involved: a ",(0,i.kt)("em",{parentName:"p"},"training request (REQ)")," and ",(0,i.kt)("em",{parentName:"p"},"training response\n(RSP)"),"."),(0,i.kt)("p",null,"The initiator node begins the broadcast BF process by transmitting REQ packets\nusing the same slot allocation as initial BF. These packets are destined towards\nthe broadcast MAC address (",(0,i.kt)("inlineCode",{parentName:"p"},"ff:ff:ff:ff:ff:ff"),"), but are otherwise identical to\nthe REQ packets used in initial BF. Two REQ packets are transmitted within each\nframe. For each Tx beam, the initiator repeats the broadcast BF REQ packets for\none BF window, or 31 frames (",(0,i.kt)("inlineCode",{parentName:"p"},"BEAM_NUM"),"). Since the channel at a responder may\nnot be configured, the initiator node performs 4 sweeps over all Tx beams to\nensure that each responder receives at least one entire set of Tx beams. The\nduration of topology scan is 124 BF windows (4*",(0,i.kt)("inlineCode",{parentName:"p"},"BEAM_NUM"),")."),(0,i.kt)("p",null,"For a receiving node to process a REQ packet, it must be in BF responder mode;\nthis is the initial state, wherein the node will sweep over the Rx beams during\neach BF window. The responder collects all REQs in a BF window and picks at most\nthe best 4 Rx beams in that window. It then sends a RSP for window ",(0,i.kt)("em",{parentName:"p"},"i")," during\nwindow ",(0,i.kt)("em",{parentName:"p"},"i+2")," to ensure that it has enough time to process the REQs. Since there\nmay be multiple responders in a topology scan, each responder randomly picks a\nframe in the BF window to transmit the packet (instead of using a fixed frame,\nlike in initial BF). The RSP will be sent using the best Rx beam as the Tx beam.\nMeanwhile, the initiator examines all frames for possible RSPs. A single scan\ninstance can process up to 15 responders (",(0,i.kt)("inlineCode",{parentName:"p"},"MAX_NUM_TOPO_RESP"),"); any additional\nresponders are ignored."),(0,i.kt)("p",null,"An example run of the entire broadcast beamforming procedure is depicted in the\ndiagram below."),(0,i.kt)("p",{align:"center"},(0,i.kt)("img",{src:"/figures/TopoScan.svg",width:"1000"})),(0,i.kt)("h3",{id:"training-packet-formats"},"Training Packet Formats"),(0,i.kt)("p",null,"The format of the REQ packet is identical to those used in initial beamforming,\nas mentioned above."),(0,i.kt)("p",null,"The RSP packet contains additional fields carrying topology-related information:"),(0,i.kt)("pre",null,(0,i.kt)("code",{parentName:"pre",className:"language-c"},"typedef struct {\n    urTrnRes_t urTrnResp;\n    usint8 frmNumInBfWin;\n    tgfGpsPos pos;\n    usint8 adjAddrs[ETH_ADDR_LEN * TGF_MAX_LOC_ADJ];\n} __attribute__((__packed__)) urTopoTrnResp_t;\n")),(0,i.kt)("p",null,"Descriptions of the fields are as follows:"),(0,i.kt)("ul",null,(0,i.kt)("li",{parentName:"ul"},(0,i.kt)("inlineCode",{parentName:"li"},"urTrnResp")," - The RSP from initial beamforming, which includes the ",(0,i.kt)("inlineCode",{parentName:"li"},"uRoute"),"\ninformation."),(0,i.kt)("li",{parentName:"ul"},(0,i.kt)("inlineCode",{parentName:"li"},"frmNumInBfWin")," - The frame number of the response."),(0,i.kt)("li",{parentName:"ul"},(0,i.kt)("inlineCode",{parentName:"li"},"pos")," - The GPS position of the responder. This is read from the stored value\nset by the ",(0,i.kt)("inlineCode",{parentName:"li"},"TG_SB_GPS_SET_SELF_POS")," ioctl, which originates either from\n",(0,i.kt)("inlineCode",{parentName:"li"},"driver-if")," (on Puma) or ",(0,i.kt)("inlineCode",{parentName:"li"},"ublox-gps")," (on Rev5)."),(0,i.kt)("li",{parentName:"ul"},(0,i.kt)("inlineCode",{parentName:"li"},"adjAddrs")," - The MAC addresses of wired neighbors and other local radios on\nthe responder node. The firmware populates this by sending a\n",(0,i.kt)("inlineCode",{parentName:"li"},"NB_OPENR_ADJ_REQ")," request to user-space (handled by the E2E minion's\n",(0,i.kt)("inlineCode",{parentName:"li"},"OpenrClientApp"),").")),(0,i.kt)("h3",{id:"usage"},"Usage"),(0,i.kt)("p",null,"Topology scans can be managed using the standard scan methods on the E2E\ncontroller."),(0,i.kt)("pre",null,(0,i.kt)("code",{parentName:"pre",className:"language-bash"},"# Start a topology scan\n$ tg scan start -t topo --tx <tx_node> -d <delay>\n\n# Retrieve the scan results ('topoResps' in ScanResp)\n$ tg scan status\n")),(0,i.kt)("p",null,(0,i.kt)("em",{parentName:"p"},"It is recommended to wait at least 4 seconds after a topology scan before\nperforming subsequent beamforming operations.")," This is because each responder\nwill record the initiator in its list of stations during a topology scan, and\nwill not process any other beamforming requests. The topology scan takes\napproximately 1.5 seconds, and the responder may take at most 2.5 seconds to\nremove the initiator from its station list."),(0,i.kt)("p",null,"Nodes can be configured to disallow initiating or responding to topology scans\nvia the firmware config field ",(0,i.kt)("inlineCode",{parentName:"p"},"radioParamsBase.fwParams.topoScanEnable"),"."))}u.isMDXComponent=!0}}]);