pragma ComponentBehavior: Bound
import QtQuick
import Conflux.Style
import "../components"
Flow {
    id: root
    required property var catalogue
    property bool instanceLocked: false
    spacing: 5
    function tagTitle(key) {
        const tag=(catalogue.options.tags || []).concat(catalogue.options.curseforgeTags || []).find(function(item) { return item.key===key })
        return (tag ? tag.title : key.slice(key.indexOf(":")+1)).replace(/-/g," ")
    }
    function remove(key,value) {
        let change={};change[key]=(catalogue.filters[key] || []).filter(function(item){return item!==value});catalogue.configure(change)
    }
    Repeater { model: root.catalogue.filters.versions || []; delegate: FilterChip { required property string modelData; objectName: "activeGameFilter"; text: modelData; locked: root.instanceLocked; onRemoved: root.remove("versions",modelData) } }
    Repeater { model: root.catalogue.filters.type === "mod" ? (root.catalogue.filters.loaders || []) : []; delegate: FilterChip { required property string modelData; text: Branding.label(modelData); onRemoved: root.remove("loaders",modelData) } }
    Repeater { model: root.catalogue.filters.tags || []; delegate: FilterChip { required property string modelData; text: root.tagTitle(modelData); onRemoved: root.remove("tags",modelData) } }
    FilterChip { objectName: "activeHideInstalledFilter"; visible: root.catalogue.hideInstalledActive; text: "Hide installed"; onRemoved: root.catalogue.configure({hideInstalled:false}) }
    FilterChip { visible: !!root.catalogue.filters.hidden; text: "Hidden content"; onRemoved: root.catalogue.configure({hidden:false}) }
}
