SELECT [Id]਍      ⴀⴀⰀ嬀吀椀琀氀攀崀ഀഀ
      --,[Summary]਍      ⴀⴀⰀ嬀䜀愀洀攀一愀洀攀崀ഀഀ
      ,[Status]਍      ⴀⴀⰀ嬀吀椀洀攀伀昀䌀爀愀猀栀崀ഀഀ
      --,[ChangeListVersion]਍      ⴀⴀⰀ嬀倀氀愀琀昀漀爀洀一愀洀攀崀ഀഀ
      --,[EngineMode]਍      Ⰰ嬀䐀攀猀挀爀椀瀀琀椀漀渀崀ഀഀ
      ,[RawCallStack]਍      Ⰰ嬀倀愀琀琀攀爀渀崀ഀഀ
      --,[CommandLine]਍      ⴀⴀⰀ嬀䌀漀洀瀀甀琀攀爀一愀洀攀崀ഀഀ
      --,[Selected]਍      Ⰰ嬀䘀椀砀攀搀䌀栀愀渀最攀䰀椀猀琀崀ഀഀ
      --,[LanguageExt]਍      ⴀⴀⰀ嬀䴀漀搀甀氀攀崀ഀഀ
      --,[BuildVersion]਍      ⴀⴀⰀ嬀䈀愀猀攀䐀椀爀崀ഀഀ
      --,[Version]਍      Ⰰ嬀唀猀攀爀一愀洀攀崀ഀഀ
      ,[TTPID]਍      ⴀⴀⰀ嬀䄀甀琀漀刀攀瀀漀爀琀攀爀䤀䐀崀ഀഀ
      --,[Processed]਍      ⴀⴀⰀ嬀䠀愀猀䰀漀最䘀椀氀攀崀ഀഀ
      --,[HasMiniDumpFile]਍      ⴀⴀⰀ嬀䠀愀猀嘀椀搀攀漀䘀椀氀攀崀ഀഀ
      --,[HasDiagnosticsFile]਍      ⴀⴀⰀ嬀䠀愀猀一攀眀䰀漀最䘀椀氀攀崀ഀഀ
      --,[Branch]਍      ⴀⴀⰀ嬀䌀爀愀猀栀吀礀瀀攀崀ഀഀ
      --,[UserNameId]਍      ⴀⴀⰀ嬀䠀愀猀䴀攀琀愀䐀愀琀愀崀ഀഀ
      --,[SourceContext]਍      ⴀⴀⰀ嬀䔀瀀椀挀䄀挀挀漀甀渀琀䤀搀崀ഀഀ
      --,[EngineVersion]਍䘀刀伀䴀 嬀䌀爀愀猀栀刀攀瀀漀爀琀崀⸀嬀搀戀漀崀⸀嬀䌀爀愀猀栀攀猀崀ഀഀ
where [TimeOfCrash] > CAST(GETDATE() AS DATE) ਍ⴀⴀ眀栀攀爀攀 嬀椀搀崀 㴀 㐀㐀㈀㔀㘀㔀ഀഀ
--where [EpicAccountId] is not null and buildversion != '4.5.0.0' and buildversion != '4.5.1.0'਍ⴀⴀ眀栀攀爀攀 嬀䐀攀猀挀爀椀瀀琀椀漀渀崀 㴀 ✀匀攀渀琀 椀渀 琀栀攀 甀渀愀琀琀攀渀搀攀搀 洀漀搀攀✀ഀഀ
--where buildversion like '4.5.1%' and [ChangeListVersion] != '0' and [PlatformName] not like 'Mac%' and [Branch] like 'UE4-Re%'਍ⴀⴀ漀爀搀攀爀 戀礀 瀀氀愀琀昀漀爀洀渀愀洀攀�