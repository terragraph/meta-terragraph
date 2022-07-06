"use strict";(self.webpackChunkwebsite=self.webpackChunkwebsite||[]).push([[4195],{5239:function(e,t,r){r.r(t),r.d(t,{default:function(){return H}});var a=r(3117),n=r(102),l=r(7294),i=r(962),o=r(9325),c=r(6010),s=r(3659),m=r(9960),u=r(2949),d=r(2263),p=r(4996),g="heroBanner_UJJx",h="heroVideo_i0RN",w="heroText_yE6R",b="heroTitle_ohkl",f="buttons_pzbO",E="getStartedButton_HGFM",v="heroGithubButton_P9Gi",k="heroButton_jgNU",y="homeContainer_Uyk3",Z="imgContainer_t8Ns",N="flexContainer_aanS",U="flexCenter_WIIO",T="featureImage_yA8i",_="partner_b5by",C="partnerImage_fveN",S="partnerModalImage_yKGS",G="modalBackdrop_njyV",M="modal_V0wS",D="modalContainer_oFEm",F="modalContents_z_ya",x=["children"];function I(e){var t=e.imageUrl,r=e.title,a=e.description,n=(0,p.Z)(t);return l.createElement("div",{className:"col col--4 text--center"},n&&l.createElement("img",{className:T,src:n,alt:r}),l.createElement("h4",null,r),l.createElement("p",null,a))}var z=[{name:"Radwin",imageUrl:"web/partner-radwin.png",href:"https://www.radwin.com/",products:[{name:"TerraWIN\u2122 Client Node",href:"https://www.radwin.com/terrawin-60ghz/"},{name:"TerraWIN\u2122 Distribution Node",href:"https://www.radwin.com/terrawin-60ghz/"}]},{name:"Siklu",imageUrl:"web/partner-siklu.png",href:"https://www.siklu.com/",products:[{name:"MultiHaul\u2122 TG Node N366",href:"https://go.siklu.com/multihaul-tg-node-n366-datasheet-lp"},{name:"Multihaul\u2122 TG Terminal Unit T265",href:"https://go.siklu.com/multihaul-tg-terminal-unit-t265-datasheet-lp"},{name:"Multihaul\u2122 TG LR Terminal Unit T280",href:"https://go.siklu.com/multihaul-tg-terminal-unit-t280-datasheet-lp"}]},{name:"Cambium",imageUrl:"web/partner-cambium.png",href:"https://www.cambiumnetworks.com/",products:[{name:"cnWave V5000",href:"https://www.cambiumnetworks.com/products/pmp-distribution/60-ghz-cnwave-v5000/"},{name:"cnWave V3000",href:"https://www.cambiumnetworks.com/products/pmp-distribution/60-ghz-cnwave-v3000/"},{name:"cnWave V1000",href:"https://www.cambiumnetworks.com/products/pmp-distribution/60-ghz-cnwave-v1000/"}]},{name:"MikroTik",imageUrl:"web/partner-mikrotik.png",href:"https://www.mikrotik.com/"},{name:"Edgecore",imageUrl:"web/partner-edgecore.png",href:"https://www.edge-core.com/",products:[{name:"MLTG-360",href:"https://wifi.edge-core.com/assets/Document/Datasheet/MLTG-360_Datasheet.pdf"},{name:"MLTG-CN",href:"https://wifi.edge-core.com/assets/Document/Datasheet/MLTG-CN_Datasheet.pdf"}],darkStyles:{filter:"invert(100%) hue-rotate(200deg) saturate(720%)"}},{name:"Ubiquiti",imageUrl:"web/partner-ubiquiti.png",href:"https://www.ui.com/",products:[{name:"airFiber 60 HD",href:"https://store.ui.com/collections/operator-airfiber/products/airfiber-60-hd"}]},{name:"Qualcomm",imageUrl:"web/partner-qualcomm.png",href:"https://www.qualcomm.com/",products:[{name:"QCA642x, QCA643x",href:"https://www.qualcomm.com/products/application/networking/qca6428"}]},{name:"Capgemini",imageUrl:"web/partner-capgemini.png",href:"https://www.capgemini.com/"}];function q(e){var t=e.name,r=e.imageUrl,a=e.darkStyles,n=e.onClick,i=(0,u.I)().colorMode,o=(0,p.Z)(r);return l.createElement("div",{className:(0,c.Z)("col col--4",_)},l.createElement("img",{className:C,style:"dark"===i&&a?a:null,src:o,alt:t,onClick:n}))}function B(e){var t=e.name,r=e.imageUrl,a=e.href,n=e.products,i=(0,p.Z)(r);return l.createElement("div",{className:D},l.createElement("div",{className:(0,c.Z)("col col--4",_)},l.createElement("img",{className:S,src:i,alt:t})),l.createElement("hr",null),l.createElement("div",{className:F},l.createElement("div",null,l.createElement("h3",null,"Links"),l.createElement("ul",null,l.createElement("li",null,l.createElement(m.Z,{to:a},"Partner Website")))),n&&l.createElement("div",null,l.createElement("h3",null,"Products"),l.createElement("ul",null,n.map((function(e){return l.createElement("li",{key:e.name},l.createElement(m.Z,{to:e.href},e.name))}))))))}var P={entering:"show",entered:"show"},R=function(e){var t=e.children,r=(0,n.Z)(e,x);return l.createElement(o.ZP,(0,a.Z)({},r,{timeout:150}),(function(e,r){return l.cloneElement(t,Object.assign({},r,{className:"fade "+P[e]+" "+t.props.className}))}))};function H(){var e,t,r,n=(0,d.Z)().siteConfig,o=void 0===n?{}:n,u=l.useState(!1),T=u[0],_=u[1],C=l.useState(null),S=C[0],D=C[1];return l.createElement(s.Z,{description:o.tagline},l.createElement("header",{className:(0,c.Z)("hero",g)},l.createElement("div",{className:h},l.createElement("video",{src:(0,p.Z)("web/header.mp4"),poster:(0,p.Z)("web/header.jpg"),autoPlay:!0,muted:!0,loop:!0,playsInline:!0})),l.createElement("div",{className:w},l.createElement("img",{className:b,src:(0,p.Z)("logo/terragraph-logo-full-RGB.svg"),alt:""}),l.createElement("div",{className:f},l.createElement(m.Z,{className:(0,c.Z)("button button--primary button--md shadow--md",k,E),to:(0,p.Z)("docs/runbook/Overview")},"Get Started"),l.createElement(m.Z,{className:(0,c.Z)("button button--secondary button--md shadow--md","githubButton",k,v),to:o.customFields.repoUrl},"GitHub")))),l.createElement("main",null,l.createElement("section",{className:(0,c.Z)("background1",y)},l.createElement("div",null,l.createElement("h2",null,"Deliver reliable, gigabit speeds easily and cost-effectively"),l.createElement("p",null,"Internet service providers (ISPs) are faced with limited options to offer reliable gigabit broadband services\u2014either lay costly fiber or install wireless technologies that can be unreliable and difficult to scale."),l.createElement("p",null,"To advance our efforts to bring more people online to a faster internet, Meta Connectivity created Terragraph. Terragraph is a wireless technology designed to make deploying gigabit connectivity faster and more efficient in markets where trenching fiber is difficult and cost-prohibitive. This solution can bring fiber-like speeds to your network in a matter of weeks\u2014and at a fraction of the cost."),l.createElement("div",{className:(0,c.Z)("item",Z)},l.createElement("picture",null,l.createElement("source",{srcSet:(0,p.Z)("web/terragraph-use-cases.svg"),media:"(min-width: 768px)"}),l.createElement("img",{src:(0,p.Z)("web/terragraph-use-cases-mobile.svg"),alt:""}))))),l.createElement("section",{className:(0,c.Z)("background2",y,N)},l.createElement("div",null,l.createElement("h2",null,"Features"),l.createElement("div",{className:(0,c.Z)("row",U)},(e=o.customFields,t=e.tgnmsUrl,r=e.openrUrl,[{title:"60 GHz spectrum",imageUrl:"web/icon-wireless.svg",description:l.createElement(l.Fragment,null,"Deliver multi-gigabit speeds over unlicensed wide frequency bands")},{title:"TDD/TDMA technology",imageUrl:"web/icon-signal-tower.svg",description:l.createElement(l.Fragment,null,"Scale your network over denser areas than previously possible with 60 GHz spectrum")},{title:"Open/R-based mesh design",imageUrl:"web/icon-polygon-tool.svg",description:l.createElement(l.Fragment,null,"Efficiently distribute capacity and offer carrier-grade availability to customers (",l.createElement(m.Z,{to:r},"learn more"),")")},{title:"Cloud-based NMS",imageUrl:"web/icon-desktop-cloud.svg",description:l.createElement(l.Fragment,null,"Remotely configure, upgrade and monitor your network (",l.createElement(m.Z,{to:t},"learn more"),")")},{title:"Advanced network planning",imageUrl:"web/icon-pin-area.svg",description:l.createElement(l.Fragment,null,"Streamline and automate network planning and feasibility assessments")},{title:"OEM ecosystem",imageUrl:"web/icon-app-groups.svg",description:l.createElement(l.Fragment,null,"Flexibly adopt and install equipment from a network of certified partners")}]).map((function(e){var t=e.title,r=e.imageUrl,a=e.description;return l.createElement(I,{key:t,title:t,imageUrl:r,description:a})}))))),l.createElement("section",{className:(0,c.Z)("background1",y,N)},l.createElement("div",{id:"home-partners"},l.createElement("h2",null,"Partners"),l.createElement("div",{className:(0,c.Z)("row",U)},z.map((function(e){return l.createElement(q,{key:e.name,name:e.name,imageUrl:e.imageUrl,darkStyles:e.darkStyles,onClick:function(){return D(e),void _(!0)}})}))))),l.createElement("section",{className:(0,c.Z)("background2",y)},l.createElement("div",null,l.createElement("h2",null,"Resources"),l.createElement("ul",null,l.createElement("li",null,"Looking to deploy Terragraph? Check out our"," ",l.createElement(m.Z,{to:(0,p.Z)("docs/runbook/")},"runbook"),","," ",l.createElement(m.Z,{to:(0,p.Z)("docs/whitepapers/")},"whitepapers"),", and partner websites to learn more."),l.createElement("li",null,"Developers can explore our public"," ",l.createElement(m.Z,{to:o.customFields.repoUrl},"code repository")," ","and extensive"," ",l.createElement(m.Z,{to:(0,p.Z)("docs/developer/")},"developer manual"),"."))))),l.createElement(i.Z,{show:T,onHide:function(){return _(!1)},className:(0,c.Z)("shadow--tl",M),transition:R,backdropTransition:R,renderBackdrop:function(e){return l.createElement("div",(0,a.Z)({className:G},e))},"aria-label":"Partner details"},S&&l.createElement(B,S)))}}}]);