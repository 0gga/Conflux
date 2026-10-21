pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Conflux.Style
import "../showcase"
ColumnLayout {
 id:root;objectName:"changelogView"
 required property var showcase
 property string mode:"latest"
 signal linkActivated(string url)
 signal imageRequested(string url)
 readonly property var selectedRelease:showcase.changelogRelease || ({})
 readonly property var releases:showcase.changelogVersions || []
 function latest(){if(releases.length)showcase.selectChangelog(releases[0])}
 function activate(){if(visible && mode==="latest")latest()}
 onVisibleChanged:activate()
 onModeChanged:activate()
 Component.onCompleted:activate()
 Connections {target:root.showcase;function onChanged(){if(root.visible && !root.selectedRelease.id)root.activate()}}
 SectionTabs {subtabs:true;entries:[{key:"latest",label:"Latest"},{key:"all",label:"All versions"}];current:root.mode;onSelected:function(key){root.mode=key}}
 Text {visible:!root.releases.length&&!root.showcase.busy;text:"No versions available";color:Theme.colors.muted;font.pixelSize:13}
 RowLayout {
  Layout.fillWidth:true;visible:root.mode==="all"
  SelectBox {objectName:"changelogVersionSelector";Layout.fillWidth:true;Layout.minimumWidth:0;model:root.releases.map(function(row){return row.version+" · "+Branding.label(row.provider)})
   currentIndex:Math.max(0,root.releases.findIndex(function(row){return row.id===root.selectedRelease.id&&row.provider===root.selectedRelease.provider}))
   onActivated:root.showcase.selectChangelog(root.releases[currentIndex])}
 }
 BusyIndicator {visible:root.showcase.changelogBusy;running:visible;Layout.preferredWidth:28;Layout.preferredHeight:28}
 Text {Layout.fillWidth:true;visible:!!root.showcase.changelogNotice;text:root.showcase.changelogNotice || "";wrapMode:Text.Wrap;color:Theme.colors.warning;font.pixelSize:13}
 BoundedScrollView {
  id:scroll;objectName:"changelogScroll";Layout.fillWidth:true;Layout.fillHeight:true;clip:true
  ColumnLayout {width:scroll.availableWidth;spacing:12
   Text {Layout.fillWidth:true;text:root.selectedRelease.version || "";font.pixelSize:20;font.weight:Font.DemiBold;color:Theme.colors.text;elide:Text.ElideRight}
   Text {visible:!root.showcase.changelogBusy&&!!root.selectedRelease.id&&!(root.showcase.changelogBlocks || []).length;text:"No changelog provided";font.pixelSize:13;color:Theme.colors.muted}
   DescriptionView {Layout.fillWidth:true;blocks:root.showcase.changelogBlocks || [];onLinkActivated:function(url){root.linkActivated(url)};onImageRequested:function(url){root.imageRequested(url)}}
  }
 }
}
