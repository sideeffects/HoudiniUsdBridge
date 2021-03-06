add_definitions( -DUSDHOUDINI_EXPORTS -DUSDMANTRA_EXPORTS )

set( husd_sources
    HUSD_Asset.C
    HUSD_AssetPath.C
    HUSD_BindMaterial.C
    HUSD_Blend.C
    HUSD_ChangeBlock.C
    HUSD_ConfigureLayer.C
    HUSD_ConfigurePrims.C
    HUSD_ConfigureProps.C
    HUSD_Constants.C
    HUSD_CreateMaterial.C
    HUSD_CreatePrims.C
    HUSD_CreateVariants.C
    HUSD_Cvex.C
    HUSD_CvexBindingMap.C
    HUSD_CvexCode.C
    HUSD_CvexDataCommand.C
    HUSD_CvexDataInputs.C
    HUSD_DataHandle.C
    HUSD_EditClips.C
    HUSD_EditCollections.C
    HUSD_EditCustomData.C
    HUSD_EditLayers.C
    HUSD_EditLinkCollections.C
    HUSD_EditMaterial.C
    HUSD_EditReferences.C
    HUSD_EditVariants.C
    HUSD_ErrorScope.C
    HUSD_ExpansionState.C
    HUSD_FileExpanded.C
    HUSD_FindCollections.C
    HUSD_FindInstanceIds.C
    HUSD_FindPrims.C
    HUSD_FindProps.C
    HUSD_GeoSubset.C
    HUSD_GeoUtils.C
    HUSD_GetAttributes.C
    HUSD_GetMetadata.C
    HUSD_HydraCamera.C
    HUSD_HydraField.C
    HUSD_HydraGeoPrim.C
    HUSD_HydraLight.C
    HUSD_HydraMaterial.C
    HUSD_HydraPrim.C
    HUSD_Imaging.C
    HUSD_Info.C
    HUSD_KarmaShaderTranslator.C
    HUSD_LayerCheckpoint.C
    HUSD_LayerOffset.C
    HUSD_LoadMasks.C
    HUSD_LockedStage.C
    HUSD_LockedStageRegistry.C
    HUSD_ManagePrims.C
    HUSD_Merge.C
    HUSD_MergeInto.C
    HUSD_MirrorRootLayer.C
    HUSD_ObjectHandle.C
    HUSD_ObjectImport.C
    HUSD_OutputProcessor.C
    HUSD_Overrides.C
    HUSD_Path.C
    HUSD_PathPattern.C
    HUSD_PathSet.C
    HUSD_PointPrim.C
    HUSD_Preferences.C
    HUSD_PrimHandle.C
    HUSD_PropertyHandle.C
    HUSD_Prune.C
    HUSD_PythonConverter.C
    HUSD_RendererInfo.C
    HUSD_Save.C
    HUSD_Scene.C
    HUSD_SetAttributes.C
    HUSD_SetMetadata.C
    HUSD_SetRelationships.C
    HUSD_ShaderTranslator.C
    HUSD_Skeleton.C
    HUSD_SpecHandle.C
    HUSD_Stitch.C
    HUSD_TimeCode.C
    HUSD_TimeShift.C
    HUSD_Token.C
    HUSD_Utils.C
    HUSD_Xform.C
    HUSD_XformAdjust.C

    XUSD_AttributeUtils.C
    XUSD_AutoCollection.C
    XUSD_Data.C
    XUSD_FindPrimsTask.C
    XUSD_HydraCamera.C
    XUSD_HydraField.C
    XUSD_HydraGeoPrim.C
    XUSD_HydraInstancer.C
    XUSD_HydraLight.C
    XUSD_HydraMaterial.C
    XUSD_HydraUtils.C
    XUSD_MirrorRootLayerData.C
    XUSD_OverridesData.C
    XUSD_PathPattern.C
    XUSD_PathSet.C
    XUSD_RenderSettings.C
    XUSD_RootLayerData.C
    XUSD_ViewerDelegate.C
    XUSD_Ticket.C
    XUSD_TicketRegistry.C
    XUSD_Tokens.C
    XUSD_Utils.C

    UsdHoudini/houdiniFieldAsset.cpp
    UsdHoudini/houdiniLayerInfo.cpp
    UsdHoudini/moduleDeps.cpp
    UsdHoudini/tokens.cpp
)

set( husd_hdk_headers
    HUSD_API.h
    HUSD_Asset.h
    HUSD_AssetPath.h
    HUSD_BindMaterial.h
    HUSD_Blend.h
    HUSD_Bucket.h
    HUSD_ChangeBlock.h
    HUSD_Compositor.h
    HUSD_ConfigureLayer.h
    HUSD_ConfigurePrims.h
    HUSD_ConfigureProps.h
    HUSD_Constants.h
    HUSD_CreateMaterial.h
    HUSD_CreatePrims.h
    HUSD_CreateVariants.h
    HUSD_Cvex.h
    HUSD_CvexBindingMap.h
    HUSD_CvexCode.h
    HUSD_CvexDataCommand.h
    HUSD_CvexDataInputs.h
    HUSD_DataHandle.h
    HUSD_EditClips.h
    HUSD_EditCollections.h
    HUSD_EditCustomData.h
    HUSD_EditLayers.h
    HUSD_EditLinkCollections.h
    HUSD_EditMaterial.h
    HUSD_EditReferences.h
    HUSD_EditVariants.h
    HUSD_ErrorScope.h
    HUSD_ExpansionState.h
    HUSD_FileExpanded.h
    HUSD_FindCollections.h
    HUSD_FindInstanceIds.h
    HUSD_FindPrims.h
    HUSD_FindProps.h
    HUSD_GeoSubset.h
    HUSD_GeoUtils.h
    HUSD_GetAttributes.h
    HUSD_GetMetadata.h
    HUSD_Imaging.h
    HUSD_Info.h
    HUSD_KarmaShaderTranslator.h
    HUSD_LayerCheckpoint.h
    HUSD_LayerOffset.h
    HUSD_LoadMasks.h
    HUSD_LockedStage.h
    HUSD_LockedStageRegistry.h
    HUSD_ManagePrims.h
    HUSD_Merge.h
    HUSD_MergeInto.h
    HUSD_MirrorRootLayer.h
    HUSD_ObjectHandle.h
    HUSD_ObjectImport.h
    HUSD_OutputProcessor.h
    HUSD_Overrides.h
    HUSD_Path.h
    HUSD_PathPattern.h
    HUSD_PathSet.h
    HUSD_PointPrim.h
    HUSD_Preferences.h
    HUSD_PrimHandle.h
    HUSD_PropertyHandle.h
    HUSD_Prune.h
    HUSD_PythonConverter.h
    HUSD_RendererInfo.h
    HUSD_Save.h
    HUSD_Scene.h
    HUSD_SetAttributes.h
    HUSD_SetMetadata.h
    HUSD_SetRelationships.h
    HUSD_ShaderTranslator.h
    HUSD_Skeleton.h
    HUSD_SpecHandle.h
    HUSD_Stitch.h
    HUSD_TimeCode.h
    HUSD_TimeShift.h
    HUSD_Token.h
    HUSD_Utils.h
    HUSD_Xform.h
    HUSD_XformAdjust.h

    XUSD_AttributeUtils.h
    XUSD_AutoCollection.h
    XUSD_Data.h
    XUSD_DataLock.h
    XUSD_FindPrimsTask.h
    XUSD_Format.h
    XUSD_HydraInstancer.h
    XUSD_HydraRenderBuffer.h
    XUSD_HydraUtils.h
    XUSD_MirrorRootLayerData.h
    XUSD_OverridesData.h
    XUSD_PathPattern.h
    XUSD_PathSet.h
    XUSD_PerfMonAutoCookEvent.h
    XUSD_RenderSettings.h
    XUSD_RootLayerData.h
    XUSD_Ticket.h
    XUSD_TicketRegistry.h
    XUSD_Tokens.h
    XUSD_Utils.h
)

