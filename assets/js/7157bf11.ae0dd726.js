"use strict";(self.webpackChunkwebsite=self.webpackChunkwebsite||[]).push([[2635],{3905:(e,n,t)=>{t.d(n,{Zo:()=>u,kt:()=>p});var a=t(7294);function r(e,n,t){return n in e?Object.defineProperty(e,n,{value:t,enumerable:!0,configurable:!0,writable:!0}):e[n]=t,e}function o(e,n){var t=Object.keys(e);if(Object.getOwnPropertySymbols){var a=Object.getOwnPropertySymbols(e);n&&(a=a.filter((function(n){return Object.getOwnPropertyDescriptor(e,n).enumerable}))),t.push.apply(t,a)}return t}function i(e){for(var n=1;n<arguments.length;n++){var t=null!=arguments[n]?arguments[n]:{};n%2?o(Object(t),!0).forEach((function(n){r(e,n,t[n])})):Object.getOwnPropertyDescriptors?Object.defineProperties(e,Object.getOwnPropertyDescriptors(t)):o(Object(t)).forEach((function(n){Object.defineProperty(e,n,Object.getOwnPropertyDescriptor(t,n))}))}return e}function s(e,n){if(null==e)return{};var t,a,r=function(e,n){if(null==e)return{};var t,a,r={},o=Object.keys(e);for(a=0;a<o.length;a++)t=o[a],n.indexOf(t)>=0||(r[t]=e[t]);return r}(e,n);if(Object.getOwnPropertySymbols){var o=Object.getOwnPropertySymbols(e);for(a=0;a<o.length;a++)t=o[a],n.indexOf(t)>=0||Object.prototype.propertyIsEnumerable.call(e,t)&&(r[t]=e[t])}return r}var l=a.createContext({}),c=function(e){var n=a.useContext(l),t=n;return e&&(t="function"==typeof e?e(n):i(i({},n),e)),t},u=function(e){var n=c(e.components);return a.createElement(l.Provider,{value:n},e.children)},d={inlineCode:"code",wrapper:function(e){var n=e.children;return a.createElement(a.Fragment,{},n)}},m=a.forwardRef((function(e,n){var t=e.components,r=e.mdxType,o=e.originalType,l=e.parentName,u=s(e,["components","mdxType","originalType","parentName"]),m=c(t),p=r,h=m["".concat(l,".").concat(p)]||m[p]||d[p]||o;return t?a.createElement(h,i(i({ref:n},u),{},{components:t})):a.createElement(h,i({ref:n},u))}));function p(e,n){var t=arguments,r=n&&n.mdxType;if("string"==typeof e||r){var o=t.length,i=new Array(o);i[0]=m;var s={};for(var l in n)hasOwnProperty.call(n,l)&&(s[l]=n[l]);s.originalType=e,s.mdxType="string"==typeof e?e:r,i[1]=s;for(var c=2;c<o;c++)i[c]=t[c];return a.createElement.apply(null,i)}return a.createElement.apply(null,t)}m.displayName="MDXCreateElement"},9685:(e,n,t)=>{t.r(n),t.d(n,{assets:()=>l,contentTitle:()=>i,default:()=>d,frontMatter:()=>o,metadata:()=>s,toc:()=>c});var a=t(7462),r=(t(7294),t(3905));const o={},i="Testing and Measurements",s={unversionedId:"runbook/Testing",id:"runbook/Testing",title:"Testing and Measurements",description:"This document describes the testing and measurement capabilities of Terragraph.",source:"@site/../docs/runbook/Testing.md",sourceDirName:"runbook",slug:"/runbook/Testing",permalink:"/docs/runbook/Testing",draft:!1,editUrl:"https://github.com/terragraph/meta-terragraph/edit/main/docs/../docs/runbook/Testing.md",tags:[],version:"current",frontMatter:{},sidebar:"runbookSidebar",previous:{title:"Routing and Traffic Engineering",permalink:"/docs/runbook/Routing"},next:{title:"Troubleshooting",permalink:"/docs/runbook/Troubleshooting"}},l={},c=[{value:"Scans",id:"scans",level:2},{value:"Background",id:"background",level:3},{value:"Scan Types",id:"scan-types",level:3},{value:"Initial Beamforming (IBF)",id:"initial-beamforming-ibf",level:4},{value:"Periodic Beamforming (PBF)",id:"periodic-beamforming-pbf",level:4},{value:"Interference Measurement (IM)",id:"interference-measurement-im",level:4},{value:"Runtime Calibration (RTCAL)",id:"runtime-calibration-rtcal",level:4},{value:"Coordinated Beamforming (CBF)",id:"coordinated-beamforming-cbf",level:4},{value:"Scan Scheduling",id:"scan-scheduling",level:3},{value:"Slotmap",id:"slotmap",level:4},{value:"Scheduling groups",id:"scheduling-groups",level:4},{value:"Manual and Automated Scans",id:"manual-and-automated-scans",level:3},{value:"Manual",id:"manual",level:4},{value:"Automated",id:"automated",level:4},{value:"Scan Results",id:"scan-results",level:4}],u={toc:c};function d(e){let{components:n,...t}=e;return(0,r.kt)("wrapper",(0,a.Z)({},u,t,{components:n,mdxType:"MDXLayout"}),(0,r.kt)("h1",{id:"testing-and-measurements"},"Testing and Measurements"),(0,r.kt)("p",null,"This document describes the testing and measurement capabilities of Terragraph."),(0,r.kt)("a",{id:"testing-scans"}),(0,r.kt)("h2",{id:"scans"},"Scans"),(0,r.kt)("h3",{id:"background"},"Background"),(0,r.kt)("p",null,"Association between two Terragraph nodes begins with initial beamforming.\nInitial beamforming does a sweep of transmit (Tx) and receive (Rx) beams on a\ncoarse, 2x grid, aiming to find the best combination of beams in terms of SNR.\nThose beams are used when the link is associated."),(0,r.kt)("p",null,"A scan is an opportunity to measure SNR and interference and optionally to\nrefine beams once the link is associated in a dedicated time slot. Scans are\ndone for multiple reasons:"),(0,r.kt)("ul",null,(0,r.kt)("li",{parentName:"ul"},(0,r.kt)("strong",{parentName:"li"},"Association")," - The initial beamforming scan is required to associate."),(0,r.kt)("li",{parentName:"ul"},(0,r.kt)("strong",{parentName:"li"},"Refinement")," - Other types of scans can refine the 2x grid used in initial\nbeamforming. Additionally, scans are used to adjust beams because\nconditions change over time."),(0,r.kt)("li",{parentName:"ul"},(0,r.kt)("strong",{parentName:"li"},"Measurement and Analysis")," - For example, scans can measure\nself-interference across the network."),(0,r.kt)("li",{parentName:"ul"},(0,r.kt)("strong",{parentName:"li"},"Debugging")," - If a particular link is underperforming (e.g., the data rate\nis too low, SNR is too low, or power is too high), one tool for debugging\nis to run a manual scan (PBF). Afterwards, scan results are collected and\ncan be viewed in the NMS UI.")),(0,r.kt)("p",null,"During a scan opportunity, there is typically one transmitter (initiator) and\none or more receivers (responders). The initiator and all responders sweep over\na set of beams and report results back to the E2E controller. The initiator\nreport contains only metadata about the scan; the responder report includes the\nmeasured SNR and RSSI for each Tx/Rx beam combination. SNR can only be measured\nif the receiver is able to detect the packet; the receiver sensitivity is around\n-10dB SNR."),(0,r.kt)("p",null,"Scans are generally either wide or selective. A wide scan evaluates the entire\nrange of beams, while a selective scan evaluates a narrower range of beams\nspecific to the scan type. Selective scans cause less interference over the\nshort duration of the scan."),(0,r.kt)("p",null,"The transmit power of the initiator can be specified, but more commonly, the\ntransmitter in the scan chooses the transmit power based on the transmit power\nit is currently using to send data. All nodes run Transmit Power Control (TPC),\nso the power changes over time. When selecting the power for a scan, the node\nuses a biased average of the currently used transmit power. It is biased toward\nhigher transmit power in order to measure worst-case interference. If a node has\nmultiple attached clients (P2MP), then the transmit power is the maximum over\nall links."),(0,r.kt)("p",null,"Scans are initiated by an E2E controller command or API call. By default, no\nscans are run."),(0,r.kt)("h3",{id:"scan-types"},"Scan Types"),(0,r.kt)("h4",{id:"initial-beamforming-ibf"},"Initial Beamforming (IBF)"),(0,r.kt)("p",null,'IBF scans are run by the firmware during ignition, without involving the E2E\ncontroller. The scan finds a good beam angle, but not the "best beam" because it\nneeds to complete quickly. IBF runs using the maximum allowed transmit power.'),(0,r.kt)("h4",{id:"periodic-beamforming-pbf"},"Periodic Beamforming (PBF)"),(0,r.kt)("p",null,'PBF scans find the best beam angle for the best SNR. These need to be run\nperiodically since temperature, weather, and other changes affect the SNR of the\nselected beam.  A "fine" PBF scan operates over the entire range of beam angles\n(roughly +/- 45 degrees), whereas a "selective" PBF scan operates only over the\ncurrent beam +/- 1.4 degrees.'),(0,r.kt)("h4",{id:"interference-measurement-im"},"Interference Measurement (IM)"),(0,r.kt)("p",null,"IM scans measure interference between nearby nodes (not necessarily connected by\na link). These use the same transmit power calculation as PBF."),(0,r.kt)("h4",{id:"runtime-calibration-rtcal"},"Runtime Calibration (RTCAL)"),(0,r.kt)("p",null,"In contrast to PBF, which attempts to find the best beam angle based on a fine\ngrid, RTCAL makes very fine adjustments to the antenna weights without changing\nthe beam angle. Consider the following analogy \u2014 if Terragraph were a movie\nprojector, then PBF would angle the projector until it faces the screen\ndirectly, and RTCAL would make fine adjustments to the focus (but the angling is\ndone electronically, without motors)."),(0,r.kt)("p",null,"RTCAL scans are ",(0,r.kt)("strong",{parentName:"p"},"not")," supported in QTI firmware. Instead, a combination of PBF\nscans and MTPO (multi-tile phase offset) calibration is used to achieve the\nsimilar goal of adjusting the inter-tile phase offsets to maximize beamforming\ngain of the antenna array."),(0,r.kt)("h4",{id:"coordinated-beamforming-cbf"},"Coordinated Beamforming (CBF)"),(0,r.kt)("p",null,"CBF scans (i.e., Interference Nulling scans) are used to reduce interference.\nThese scans are ",(0,r.kt)("strong",{parentName:"p"},"not")," currently supported in QTI firmware."),(0,r.kt)("h3",{id:"scan-scheduling"},"Scan Scheduling"),(0,r.kt)("p",null,"The amount of time it takes to run a scan for the entire network depends on the\nnetwork topology. For a network with approximately 20 or more nodes, scans will\ntypically take on the order of 10-20 minutes, but this depends on the specific\nlocations of the nodes because of the Scheduling Groups described below.\nGenerally, the time it takes to complete a full-network scan does not depend on\nthe size of the network once the network reaches over 20 nodes because scans run\nin parallel."),(0,r.kt)("p",null,"This section will explain how scans are scheduled and why it takes tens of\nminutes to complete a round of scans for the network."),(0,r.kt)("h4",{id:"slotmap"},"Slotmap"),(0,r.kt)("p",null,"At a high level, a ~52-second slotmap is defined. Within a slotmap, there are\ntypically one or two opportunities to run a given scan type (e.g., PBF or IM).\nScans can run in parallel on different Terragraph nodes, but only if the two\nscans do not cause RF interference with each other. A full network scan can take\ntens of minutes because several slotmaps are needed to cover all nodes in the\nnetwork."),(0,r.kt)("p",null,"Scans can be initiated manually for a single link (for debugging), but they are\nmore commonly scheduled periodically for the entire network (e.g., every 6\nhours). Scans need to be scheduled according to certain constraints, and a node\ncannot perform more than one scan at a time. Because scans involve a sweep of\nbeams, simultaneous scans should not be run on nodes that are close to each\nother in order to avoid erroneous measurements. For example, during a PBF scan\nbetween nodes A\u2192B, a simultaneous PBF scan between nodes C\u2192D would only be\nscheduled if nodes A and B are physically far away from  nodes C and D."),(0,r.kt)("p",null,"A ",(0,r.kt)("em",{parentName:"p"},"slotmap")," is defined to coordinate scan scheduling as shown below:"),(0,r.kt)("p",{align:"center"},(0,r.kt)("img",{src:"/figures/bftd.jpg",width:"1200"}),(0,r.kt)("br",null),(0,r.kt)("em",null,"Slotmap Definition")),(0,r.kt)("ul",null,(0,r.kt)("li",{parentName:"ul"},(0,r.kt)("inlineCode",{parentName:"li"},"Nf")," = 4 frames (1 superframe = 1.6ms)"),(0,r.kt)("li",{parentName:"ul"},(0,r.kt)("inlineCode",{parentName:"li"},"Ns")," = 16 superframes (one BWGD = 25.6ms)"),(0,r.kt)("li",{parentName:"ul"},(0,r.kt)("inlineCode",{parentName:"li"},"Nb")," = 16 BWGDs (one BFTD = 409.6ms)"),(0,r.kt)("li",{parentName:"ul"},(0,r.kt)("inlineCode",{parentName:"li"},"Nm")," = 128 BFTDs (one slotmap ~= 52.5 seconds)")),(0,r.kt)("p",null,"An example slotmap is shown below for the different scan types."),(0,r.kt)("p",{align:"center"},(0,r.kt)("img",{src:"/figures/slotmap_example.svg",width:"1000"}),(0,r.kt)("br",null),(0,r.kt)("em",null,"Slotmap Scheduling Example")),(0,r.kt)("p",null,"For example, two PBF scans can run in each slotmap. The slotmap is configurable."),(0,r.kt)("h4",{id:"scheduling-groups"},"Scheduling groups"),(0,r.kt)("p",null,"Multiple scans of the same type (e.g., PBF) can run at the same time on\ndifferent links/nodes in the network in groups. But nodes within a group have to\nbe separated by a minimum distance to avoid interference. The distance is\nconfigurable (e.g., 350 meters). The E2E controller calculates scheduling groups\nbased on GPS coordinates and the configured distance."),(0,r.kt)("h3",{id:"manual-and-automated-scans"},"Manual and Automated Scans"),(0,r.kt)("h4",{id:"manual"},"Manual"),(0,r.kt)("p",null,"A manual scan is a one-shot scan (either for a particular set of nodes or once\nfor the entire network), used primarily for debugging a link. Scans can be\nmanually initiated using the TG CLI:"),(0,r.kt)("pre",null,(0,r.kt)("code",{parentName:"pre",className:"language-bash"},"# PBF scan on link node1->node2\n$ tg scan start -t pbf --tx node1 --rx node2\n\n# IM scan from node1->node{2,3,4}, even if there are no links between them\n$ tg scan start -t im --tx node1 --rx node2 --rx node3 --rx node4\n\n# Run IM scan on whole network (more on this later)\n$ tg scan start -t im\n")),(0,r.kt)("h4",{id:"automated"},"Automated"),(0,r.kt)("p",null,"Scans can be automatically initiated by the E2E controller on a schedule set\nusing the TG CLI:"),(0,r.kt)("pre",null,(0,r.kt)("code",{parentName:"pre",className:"language-bash"},"# Start whole-network PBF scans every 30 minutes, starting 30 minutes from now\n$ tg scan schedule --pbf 1800\n\n# Start whole-network IM scans every 20 minutes, starting 20 minutes from now\n$ tg scan schedule --im 1200\n")),(0,r.kt)("p",null,"Scanning impacts network performance for the short duration of a scan (between\n51ms and 1.6s depending on the scan type), and should be run infrequently\n(typically every 6 to 12 hours). During a scan, ~60us time slots (shown in green\nin the slotmap definition figure above) are reserved for scans and therefore not\nused to transmit data; in the 200us subframe, this is approximately a 30%\noverhead. Depending on the scan type and network topology, a scan can also cause\ninterference on neighboring links that are not involved in the scan. This is\nbecause a scan sweeps over multiple beams and therefore can directly transmit\nenergy in a direction other than the associated link(s). Interference will not\nhave any long-term impact but can, momentarily, cause a reduction in data rate\non neighboring links."),(0,r.kt)("p",null,"By default, the controller initiates PBF, RTCAL, and CBF scans every 4 hours\n(defined in ",(0,r.kt)("inlineCode",{parentName:"p"},"/etc/e2e_config/controller_config_default.json"),")."),(0,r.kt)("h4",{id:"scan-results"},"Scan Results"),(0,r.kt)("p",null,"Completed scan results are processed by Query Service, and can be viewed in the\nNMS."))}d.isMDXComponent=!0}}]);