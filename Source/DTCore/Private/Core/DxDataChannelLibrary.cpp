#include "Core/DxDataChannelLibrary.h"

#include "Misc/Guid.h"

const FName UDxDataChannelLibrary::SchemaVersionHeader(TEXT("dx-schema-version"));
const FName UDxDataChannelLibrary::MessageIdHeader(TEXT("dx-message-id"));
const FName UDxDataChannelLibrary::SentAtHeader(TEXT("dx-sent-at"));

namespace
{
	bool IsValidSegment(const FString& Segment)
	{
		if (Segment.IsEmpty()) return false;
		for (const TCHAR Char : Segment)
		{
			const bool bAllowed = (Char >= TEXT('a') && Char <= TEXT('z')) || (Char >= TEXT('0') && Char <= TEXT('9')) || Char == TEXT('-') || Char == TEXT('_');
			if (!bAllowed) return false;
		}
		return true;
	}
}

FString UDxDataChannelLibrary::MakeDestination(const TArray<FString>& Segments)
{
	if (Segments.Num() == 0) return FString();
	for (const FString& Segment : Segments)
	{
		if (!IsValidSegment(Segment)) return FString();
	}
	return FString::Join(Segments, TEXT("."));
}

bool UDxDataChannelLibrary::IsValidDestination(const FString& Destination)
{
	TArray<FString> Segments;
	// CullEmpty=false: 빈 조각(".." 또는 앞뒤 '.')을 잡아낸다.
	Destination.ParseIntoArray(Segments, TEXT("."), false);
	return !MakeDestination(Segments).IsEmpty();
}

TMap<FName, FString> UDxDataChannelLibrary::MakeStandardHeaders(const FString& SchemaVersion, const FString& MessageId,
	const TMap<FName, FString>& Headers)
{
	TMap<FName, FString> Result = Headers;
	if (!SchemaVersion.IsEmpty() && !Result.Contains(SchemaVersionHeader))
	{
		Result.Add(SchemaVersionHeader, SchemaVersion);
	}
	if (!Result.Contains(MessageIdHeader))
	{
		Result.Add(MessageIdHeader, MessageId.IsEmpty() ? FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower) : MessageId);
	}
	if (!Result.Contains(SentAtHeader))
	{
		Result.Add(SentAtHeader, FDateTime::UtcNow().ToIso8601());
	}
	return Result;
}
